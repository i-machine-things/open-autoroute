#include "openautoroute/s57.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <utility>

namespace oar {
namespace {

// ISO 8211 framing, then the fixed binary layouts S-57 Part 3 defines for the fields we read. Those layouts are
// hard-coded instead of interpreting each file's data descriptive record: S-57 fixes them, and it keeps this small.

constexpr uint8_t kFieldTerminator = 0x1E;
constexpr uint8_t kUnitTerminator = 0x1F;

struct Field {
    std::string tag;
    const uint8_t* data;
    size_t size;  // excludes the field terminator
};

struct Record {
    char leaderId = 'D';
    std::vector<Field> fields;
    const Field* find(const char* tag) const {
        for (const Field& f : fields) {
            if (f.tag == tag) return &f;
        }
        return nullptr;
    }
};

bool parseDigits(const uint8_t* p, size_t n, size_t& out) {
    out = 0;
    for (size_t i = 0; i < n; ++i) {
        if (p[i] < '0' || p[i] > '9') return false;
        out = out * 10 + (p[i] - '0');
    }
    return true;
}

uint32_t le32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24); }
int32_t sle32(const uint8_t* p) { return static_cast<int32_t>(le32(p)); }
uint16_t le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

bool readRecord(const uint8_t* buf, size_t avail, Record& rec, size_t& consumed, std::string& err) {
    size_t len = 0, base = 0, sfl = 0, sfp = 0, stl = 0;
    if (avail < 24 || !parseDigits(buf, 5, len) || len < 24 || len > avail) {
        err = "bad ISO 8211 record length";
        return false;
    }
    if (!parseDigits(buf + 12, 5, base) || !parseDigits(buf + 20, 1, sfl) || !parseDigits(buf + 21, 1, sfp) ||
        !parseDigits(buf + 23, 1, stl) || sfl == 0 || sfp == 0 || stl == 0 || base < 25 || base > len) {
        err = "bad ISO 8211 leader";
        return false;
    }
    rec.leaderId = static_cast<char>(buf[6]);
    rec.fields.clear();
    const size_t entry = stl + sfl + sfp;
    for (size_t p = 24; p < base - 1; p += entry) {
        size_t flen = 0, fpos = 0;
        if (p + entry > base - 1 || !parseDigits(buf + p + stl, sfl, flen) ||
            !parseDigits(buf + p + stl + sfl, sfp, fpos) || flen == 0 || base + fpos + flen > len) {
            err = "bad ISO 8211 directory entry";
            return false;
        }
        rec.fields.push_back({std::string(reinterpret_cast<const char*>(buf + p), stl), buf + base + fpos, flen - 1});
    }
    consumed = len;
    return true;
}

using Pt = std::pair<int32_t, int32_t>;  // (y, x) in COMF units, as stored

struct VecRec {
    std::vector<Pt> pts;                    // SG2D vertices (a connected node has exactly one)
    std::vector<std::array<int32_t, 3>> p3;  // SG3D triples (soundings), z in SOMF units
    bool hasBegin = false, hasEnd = false;
    uint64_t begin = 0, end = 0;
};

struct SpatialRef {
    uint64_t key;
    uint8_t ornt;  // 1 forward, 2 reverse
    uint8_t usag;  // 1 exterior, 2 interior (hole), 3 exterior truncated by the data limit
};

struct FeatRec {
    uint8_t prim = 0;
    uint16_t objl = 0;
    std::vector<std::pair<uint16_t, std::string>> attrs;
    std::vector<SpatialRef> spatial;
};

uint64_t keyOf(uint8_t rcnm, uint32_t rcid) { return (static_cast<uint64_t>(rcnm) << 32) | rcid; }

// Object class codes, verified against OpenCPN's s57objectclasses.csv (the IHO S-57 object catalogue). Only classes the router acts on
// are kept; docs/S57_OBJECTS.md lists every class present in the NOAA data and how each is meant to be handled.
const char* classOf(uint16_t objl) {
    switch (objl) {
        case 4: return "ACHARE";
        case 5: return "BCNCAR";
        case 6: return "BCNISD";
        case 7: return "BCNLAT";
        case 11: return "BRIDGE";
        case 14: return "BOYCAR";
        case 16: return "BOYISD";
        case 17: return "BOYLAT";
        case 21: return "CBLOHD";
        case 26: return "CAUSWY";
        case 27: return "CTNARE";
        case 30: return "COALNE";
        case 34: return "CONVYR";
        case 38: return "DAMCON";
        case 42: return "DEPARE";
        case 43: return "DEPCNT";
        case 46: return "DRGARE";
        case 47: return "DRYDOC";
        case 48: return "DMPGRD";
        case 49: return "DYKCON";
        case 51: return "FAIRWY";
        case 52: return "FNCLNE";
        case 55: return "FSHFAC";
        case 57: return "FLODOC";
        case 61: return "GATCON";
        case 79: return "LOKBSN";
        case 62: return "GRIDRN";
        case 65: return "HULKES";
        case 71: return "LNDARE";
        case 82: return "MARCUL";
        case 83: return "MIPARE";
        case 84: return "MORFAC";
        case 86: return "OBSTRN";
        case 87: return "OFSPLF";
        case 88: return "OSPARE";
        case 89: return "OILBAR";
        case 90: return "PILPNT";
        case 93: return "PIPOHD";
        case 95: return "PONTON";
        case 96: return "PRCARE";
        case 97: return "PRDARE";
        case 98: return "PYLONS";
        case 107: return "RAPIDS";
        case 112: return "RESARE";
        case 120: return "SPLARE";
        case 122: return "SLCONS";
        case 129: return "SOUNDG";
        case 145: return "TSELNE";
        case 146: return "TSSBND";
        case 147: return "TSSCRS";
        case 148: return "TSSLPT";
        case 149: return "TSSRON";
        case 150: return "TSEZNE";
        case 153: return "UWTROC";
        case 154: return "UNSARE";
        case 156: return "WATTUR";
        case 157: return "WATFAL";
        case 158: return "WEDKLP";
        case 159: return "WRECKS";
        default: return nullptr;
    }
}

// Attribute codes, from s57attributes.csv.
constexpr uint16_t kAttrInform = 102, kAttrCatdpg = 23, kAttrCatcam = 13, kAttrCatrea = 56, kAttrRestrn = 131, kAttrVerclr = 181, kAttrVerccl = 182, kAttrWatlev = 187;
constexpr uint16_t kAttrCatlam = 36, kAttrDrval1 = 87, kAttrDrval2 = 88, kAttrOrient = 117, kAttrValdco = 174, kAttrValsou = 179;

void parseFeature(const Record& r, FeatRec& f) {
    if (const Field* frid = r.find("FRID"); frid && frid->size >= 9) {
        f.prim = frid->data[5];
        f.objl = le16(frid->data + 7);
    }
    if (const Field* attf = r.find("ATTF")) {
        size_t i = 0;
        while (i + 2 <= attf->size) {
            const uint16_t code = le16(attf->data + i);
            i += 2;
            size_t j = i;
            while (j < attf->size && attf->data[j] != kUnitTerminator) ++j;
            f.attrs.emplace_back(code, std::string(reinterpret_cast<const char*>(attf->data + i), j - i));
            i = j + 1;
        }
    }
    if (const Field* fspt = r.find("FSPT")) {
        for (size_t i = 0; i + 8 <= fspt->size; i += 8) {
            f.spatial.push_back({keyOf(fspt->data[i], le32(fspt->data + i + 1)), fspt->data[i + 5], fspt->data[i + 6]});
        }
    }
}

void parseVector(const Record& r, const Field& vrid, std::map<uint64_t, VecRec>& vecs) {
    if (vrid.size < 5) return;
    VecRec& v = vecs[keyOf(vrid.data[0], le32(vrid.data + 1))];
    if (const Field* sg = r.find("SG2D")) {
        for (size_t i = 0; i + 8 <= sg->size; i += 8) v.pts.emplace_back(sle32(sg->data + i), sle32(sg->data + i + 4));
    }
    if (const Field* sg = r.find("SG3D")) {
        for (size_t i = 0; i + 12 <= sg->size; i += 12) {
            v.p3.push_back({sle32(sg->data + i), sle32(sg->data + i + 4), sle32(sg->data + i + 8)});
        }
    }
    if (const Field* vrpt = r.find("VRPT")) {  // an edge's two end nodes: TOPI 1 = begin, 2 = end
        for (size_t i = 0; i + 9 <= vrpt->size; i += 9) {
            const uint64_t node = keyOf(vrpt->data[i], le32(vrpt->data + i + 1));
            if (vrpt->data[i + 7] == 1) { v.hasBegin = true; v.begin = node; }
            if (vrpt->data[i + 7] == 2) { v.hasEnd = true; v.end = node; }
        }
    }
}

// Full vertex list of an edge, begin node -> intermediate SG2D points -> end node, optionally reversed.
std::vector<Pt> edgePoints(const std::map<uint64_t, VecRec>& vecs, uint64_t key, bool reversed) {
    std::vector<Pt> out;
    auto it = vecs.find(key);
    if (it == vecs.end()) return out;
    const VecRec& e = it->second;
    auto nodePt = [&](bool has, uint64_t k) {
        if (!has) return;
        auto n = vecs.find(k);
        if (n != vecs.end() && !n->second.pts.empty()) out.push_back(n->second.pts.front());
    };
    nodePt(e.hasBegin, e.begin);
    out.insert(out.end(), e.pts.begin(), e.pts.end());
    nodePt(e.hasEnd, e.end);
    if (reversed) std::reverse(out.begin(), out.end());
    return out;
}

// Chain edges into polylines/rings in FSPT order, dropping the duplicated joint vertex, and start a new ring whenever
// the current one closes. A ring's hole flag comes from the usage of its first edge.
std::vector<std::pair<std::vector<Pt>, bool>> chainEdges(const std::map<uint64_t, VecRec>& vecs, const FeatRec& f,
                                                          bool splitRings) {
    std::vector<std::pair<std::vector<Pt>, bool>> rings;
    std::vector<Pt> cur;
    bool curHole = false;
    for (const SpatialRef& s : f.spatial) {
        if ((s.key >> 32) != 130) continue;  // edges only
        std::vector<Pt> pts = edgePoints(vecs, s.key, s.ornt == 2);
        if (pts.empty()) continue;
        if (cur.empty()) curHole = s.usag == 2;
        size_t from = (!cur.empty() && cur.back() == pts.front()) ? 1 : 0;
        cur.insert(cur.end(), pts.begin() + from, pts.end());
        if (splitRings && cur.size() > 2 && cur.front() == cur.back()) {
            rings.emplace_back(std::move(cur), curHole);
            cur.clear();
        }
    }
    if (!cur.empty()) rings.emplace_back(std::move(cur), curHole);
    return rings;
}

// List-valued attributes (RESTRN, CATREA) are comma-separated integers; return them as a bitmask with bit n set for value n (n < 32).
uint32_t attrMask(const FeatRec& f, uint16_t code) {
    uint32_t mask = 0;
    for (const auto& a : f.attrs) {
        if (a.first != code) continue;
        size_t i = 0;
        while (i < a.second.size()) {
            const int v = std::atoi(a.second.c_str() + i);
            if (v > 0 && v < 32) mask |= 1u << v;
            const size_t comma = a.second.find(',', i);
            if (comma == std::string::npos) break;
            i = comma + 1;
        }
    }
    return mask;
}

double attrNum(const FeatRec& f, uint16_t code) {
    for (const auto& a : f.attrs) {
        if (a.first == code && !a.second.empty()) return std::atof(a.second.c_str());
    }
    return std::numeric_limits<double>::quiet_NaN();
}

}  // namespace

bool loadS57Buffer(const std::vector<uint8_t>& bytes, ChartData& out, std::string& error) {
    out.features.clear();
    std::map<uint64_t, VecRec> vecs;
    std::vector<FeatRec> feats;
    double comf = 10000000.0, somf = 10.0;  // S-57 defaults; overridden by the cell's DSPM record

    size_t pos = 0;
    Record rec;
    while (pos < bytes.size()) {
        size_t used = 0;
        if (!readRecord(bytes.data() + pos, bytes.size() - pos, rec, used, error)) return false;
        pos += used;
        if (rec.leaderId == 'L') continue;  // data descriptive record: layouts are fixed by the standard
        if (const Field* dspm = rec.find("DSPM"); dspm && dspm->size >= 24) {
            if (le32(dspm->data + 16) != 0) comf = le32(dspm->data + 16);
            if (le32(dspm->data + 20) != 0) somf = le32(dspm->data + 20);
        } else if (const Field* vrid = rec.find("VRID")) {
            parseVector(rec, *vrid, vecs);
        } else if (rec.find("FRID")) {
            feats.emplace_back();
            parseFeature(rec, feats.back());
        }
    }

    auto toLatLon = [comf](Pt p) { return LatLon{p.first / comf, p.second / comf}; };
    auto toRing = [&](const std::vector<Pt>& pts, bool hole) {
        Ring r;
        r.hole = hole;
        for (Pt p : pts) r.points.push_back(toLatLon(p));
        return r;
    };

    for (const FeatRec& f : feats) {
        const char* cls = classOf(f.objl);
        if (!cls) continue;
        ChartFeature cf;
        cf.objectClass = cls;
        cf.drval1 = attrNum(f, kAttrDrval1);
        cf.drval2 = attrNum(f, kAttrDrval2);
        cf.valdco = attrNum(f, kAttrValdco);
        cf.valsou = attrNum(f, kAttrValsou);
        cf.orient = attrNum(f, kAttrOrient);
        cf.catlam = attrNum(f, kAttrCatlam);
        cf.watlev = attrNum(f, kAttrWatlev);
        cf.verclr = attrNum(f, kAttrVerclr);
        cf.verccl = attrNum(f, kAttrVerccl);
        cf.catcam = attrNum(f, kAttrCatcam);
        cf.restrn = attrMask(f, kAttrRestrn);
        cf.catrea = attrMask(f, kAttrCatrea);
        cf.catdpg = attrMask(f, kAttrCatdpg);
        if (cf.objectClass == "RESARE" || cf.objectClass == "CTNARE" || cf.objectClass == "MIPARE" || cf.objectClass == "DMPGRD") {
            for (const auto& a : f.attrs) {
                if (a.first == kAttrInform) cf.inform = a.second;
            }
        }

        if (f.prim == 1) {  // point: isolated node(s); SOUNDG expands to one point per sounding
            cf.geometry = Geometry::Point;
            for (const SpatialRef& s : f.spatial) {
                auto it = vecs.find(s.key);
                if (it == vecs.end()) continue;
                if (!it->second.p3.empty()) {
                    for (const auto& t : it->second.p3) {
                        ChartFeature sd = cf;
                        sd.parts.push_back(toRing({{t[0], t[1]}}, false));
                        sd.valsou = t[2] / somf;
                        out.features.push_back(std::move(sd));
                    }
                } else if (!it->second.pts.empty()) {
                    ChartFeature pt = cf;
                    pt.parts.push_back(toRing({it->second.pts.front()}, false));
                    out.features.push_back(std::move(pt));
                }
            }
            continue;
        }
        cf.geometry = f.prim == 3 ? Geometry::Area : Geometry::Line;
        for (const auto& [pts, hole] : chainEdges(vecs, f, cf.geometry == Geometry::Area)) {
            cf.parts.push_back(toRing(pts, hole));
        }
        if (!cf.parts.empty()) out.features.push_back(std::move(cf));
    }
    return true;
}

bool loadS57(const std::string& path, ChartData& out, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot open " + path;
        return false;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return loadS57Buffer(bytes, out, error);
}

}  // namespace oar
