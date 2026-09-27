#include "openautoroute_pi.h"

#include <wx/wx.h>
#include <wx/filepicker.h>
#include <wx/fileconf.h>
#include <wx/gauge.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <mutex>

using namespace oar;

namespace {

constexpr double kFeetToM = 0.3048;

// A route glyph drawn at run time, so the plugin needs no data files: a dashed line through two ends.
wxBitmap makeIcon() {
    wxBitmap bmp(32, 32);
    wxMemoryDC dc(bmp);
    dc.SetBackground(*wxTRANSPARENT_BRUSH);
    dc.Clear();
    dc.SetPen(wxPen(wxColour(30, 90, 200), 3));
    dc.DrawLine(6, 25, 13, 12);
    dc.DrawLine(13, 12, 20, 19);
    dc.DrawLine(20, 19, 26, 6);
    dc.SetBrush(wxBrush(wxColour(30, 160, 60)));
    dc.SetPen(*wxBLACK_PEN);
    dc.DrawCircle(6, 25, 4);
    dc.SetBrush(wxBrush(wxColour(220, 40, 40)));
    dc.DrawCircle(26, 6, 4);
    dc.SelectObject(wxNullBitmap);
    return bmp;
}

bool parsePoint(const wxString& text, LatLon& out) {
    double lat = 0, lon = 0;
    wxString t = text;
    t.Replace(";", ",");
    const int comma = t.Find(',');
    if (comma == wxNOT_FOUND) return false;
    if (!t.Left(comma).Trim().Trim(false).ToDouble(&lat) || !t.Mid(comma + 1).Trim().Trim(false).ToDouble(&lon)) return false;
    if (std::fabs(lat) > 90.0 || std::fabs(lon) > 180.0) return false;
    out = {lat, lon};
    return true;
}

wxString pointText(double lat, double lon) { return wxString::Format("%.5f, %.5f", lat, lon); }

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------------------

class AutoRouteDialog : public wxDialog {
public:
    AutoRouteDialog(wxWindow* parent, openautoroute_pi* owner);
    ~AutoRouteDialog() override;

    void SetFrom(double lat, double lon) { from_->SetValue(pointText(lat, lon)); }
    void SetTo(double lat, double lon) { to_->SetValue(pointText(lat, lon)); }

private:
    void OnPlan(wxCommandEvent&);
    void OnCancelOrClose(wxCommandEvent&);
    void OnShipFrom(wxCommandEvent&);
    void OnCursorFrom(wxCommandEvent&);
    void OnCursorTo(wxCommandEvent&);
    void OnUnits(wxCommandEvent&);
    void OnClose(wxCloseEvent&);
    void Save();
    void SetRunning(bool running);
    void Finish(PlanResult result);
    void AddToOpenCPN(const PlanResult& result);
    double ToMetres(double v) const { return units_->GetSelection() == 1 ? v * kFeetToM : v; }
    double FromMetres(double m) const { return units_->GetSelection() == 1 ? m / kFeetToM : m; }
    wxString Length(double m) const { return wxString::Format("%.0f %s", FromMetres(m), units_->GetSelection() == 1 ? "ft" : "m"); }

    openautoroute_pi* owner_;
    wxTextCtrl *from_, *to_, *length_, *draft_, *clearance_, *airDraft_, *report_;
    wxChoice* units_;
    wxCheckBox* sail_;
    wxDirPickerCtrl* encDir_;
    wxGauge* gauge_;
    wxStaticText* phase_;
    wxButton *plan_, *cancel_;
    std::thread worker_;
    std::atomic<bool> cancel_flag_{false};
    bool running_ = false;
    int lastUnits_ = 0;
};

AutoRouteDialog::AutoRouteDialog(wxWindow* parent, openautoroute_pi* owner)
    : wxDialog(parent, wxID_ANY, "Auto-route", wxDefaultPosition, wxSize(520, 640), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER), owner_(owner) {
    wxConfigBase* cfg = GetOCPNConfigObject();
    wxString from, to, enc, len = "12", draft = "1.5", clr = "1.0", air;
    long units = 0, sail = 0;
    if (cfg) {
        cfg->SetPath("/PlugIns/OpenAutoRoute");
        cfg->Read("From", &from);
        cfg->Read("To", &to);
        cfg->Read("EncDir", &enc);
        cfg->Read("Units", &units, 0L);
        cfg->Read("Sail", &sail, 0L);
        cfg->Read("Length", &len);
        cfg->Read("Draft", &draft);
        cfg->Read("Clearance", &clr);
        cfg->Read("AirDraft", &air);
        cfg->SetPath("/");
    }
    if (enc.IsEmpty()) enc = owner_->DefaultEncDir();

    auto* top = new wxBoxSizer(wxVERTICAL);
    auto* grid = new wxFlexGridSizer(3, 5, 5);
    grid->AddGrowableCol(1);
    auto label = [&](const wxString& t) { return new wxStaticText(this, wxID_ANY, t); };

    grid->Add(label("From (lat, lon)"), 0, wxALIGN_CENTER_VERTICAL);
    from_ = new wxTextCtrl(this, wxID_ANY, from);
    grid->Add(from_, 1, wxEXPAND);
    auto* fromButtons = new wxBoxSizer(wxHORIZONTAL);
    auto* shipBtn = new wxButton(this, wxID_ANY, "Ship", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    auto* curFrom = new wxButton(this, wxID_ANY, "Pick on chart", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    fromButtons->Add(shipBtn, 0, wxRIGHT, 3);
    fromButtons->Add(curFrom);
    grid->Add(fromButtons);

    grid->Add(label("To (lat, lon)"), 0, wxALIGN_CENTER_VERTICAL);
    to_ = new wxTextCtrl(this, wxID_ANY, to);
    grid->Add(to_, 1, wxEXPAND);
    auto* curTo = new wxButton(this, wxID_ANY, "Pick on chart", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    grid->Add(curTo);

    grid->Add(label("Units"), 0, wxALIGN_CENTER_VERTICAL);
    units_ = new wxChoice(this, wxID_ANY);
    units_->Append("Metres");
    units_->Append("Feet");
    units_->SetSelection(units == 1 ? 1 : 0);
    lastUnits_ = units_->GetSelection();
    grid->Add(units_);
    grid->AddSpacer(0);

    grid->Add(label("Vessel length"), 0, wxALIGN_CENTER_VERTICAL);
    length_ = new wxTextCtrl(this, wxID_ANY, len);
    grid->Add(length_);
    sail_ = new wxCheckBox(this, wxID_ANY, "Under sail");
    sail_->SetValue(sail != 0);
    grid->Add(sail_);

    grid->Add(label("Draft"), 0, wxALIGN_CENTER_VERTICAL);
    draft_ = new wxTextCtrl(this, wxID_ANY, draft);
    grid->Add(draft_);
    grid->AddSpacer(0);

    grid->Add(label("Extra clearance"), 0, wxALIGN_CENTER_VERTICAL);
    clearance_ = new wxTextCtrl(this, wxID_ANY, clr);
    grid->Add(clearance_);
    grid->AddSpacer(0);

    grid->Add(label("Air draft (blank = estimate)"), 0, wxALIGN_CENTER_VERTICAL);
    airDraft_ = new wxTextCtrl(this, wxID_ANY, air);
    grid->Add(airDraft_);
    grid->AddSpacer(0);
    top->Add(grid, 0, wxEXPAND | wxALL, 8);

    top->Add(label("S-57 chart folder (ENC_ROOT)"), 0, wxLEFT | wxRIGHT, 8);
    encDir_ = new wxDirPickerCtrl(this, wxID_ANY, enc, "Choose the ENC_ROOT folder", wxDefaultPosition, wxDefaultSize,
                                  wxDIRP_USE_TEXTCTRL | wxDIRP_DIR_MUST_EXIST);
    top->Add(encDir_, 0, wxEXPAND | wxALL, 8);

    phase_ = new wxStaticText(this, wxID_ANY, "Ready");
    top->Add(phase_, 0, wxLEFT | wxRIGHT, 8);
    gauge_ = new wxGauge(this, wxID_ANY, 1000);
    top->Add(gauge_, 0, wxEXPAND | wxALL, 8);

    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    plan_ = new wxButton(this, wxID_ANY, "Plan route");
    cancel_ = new wxButton(this, wxID_ANY, "Close");
    buttons->Add(plan_, 0, wxRIGHT, 6);
    buttons->Add(cancel_);
    top->Add(buttons, 0, wxALIGN_RIGHT | wxALL, 8);

    report_ = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxSize(-1, 200), wxTE_MULTILINE | wxTE_READONLY);
    top->Add(report_, 1, wxEXPAND | wxALL, 8);
    SetSizer(top);

    plan_->Bind(wxEVT_BUTTON, &AutoRouteDialog::OnPlan, this);
    cancel_->Bind(wxEVT_BUTTON, &AutoRouteDialog::OnCancelOrClose, this);
    shipBtn->Bind(wxEVT_BUTTON, &AutoRouteDialog::OnShipFrom, this);
    curFrom->Bind(wxEVT_BUTTON, &AutoRouteDialog::OnCursorFrom, this);
    curTo->Bind(wxEVT_BUTTON, &AutoRouteDialog::OnCursorTo, this);
    units_->Bind(wxEVT_CHOICE, &AutoRouteDialog::OnUnits, this);
    Bind(wxEVT_CLOSE_WINDOW, &AutoRouteDialog::OnClose, this);
}

AutoRouteDialog::~AutoRouteDialog() {
    cancel_flag_ = true;  // the planner checks this often, so the join is quick
    if (worker_.joinable()) worker_.join();
}

void AutoRouteDialog::Save() {
    wxConfigBase* cfg = GetOCPNConfigObject();
    if (!cfg) return;
    cfg->SetPath("/PlugIns/OpenAutoRoute");
    cfg->Write("From", from_->GetValue());
    cfg->Write("To", to_->GetValue());
    cfg->Write("EncDir", encDir_->GetPath());
    cfg->Write("Units", static_cast<long>(units_->GetSelection()));
    cfg->Write("Sail", static_cast<long>(sail_->GetValue() ? 1 : 0));
    cfg->Write("Length", length_->GetValue());
    cfg->Write("Draft", draft_->GetValue());
    cfg->Write("Clearance", clearance_->GetValue());
    cfg->Write("AirDraft", airDraft_->GetValue());
    cfg->SetPath("/");
    cfg->Flush();
}

void AutoRouteDialog::OnShipFrom(wxCommandEvent&) {
    if (owner_->HaveShip()) SetFrom(owner_->ShipLat(), owner_->ShipLon());
    else wxMessageBox("OpenCPN has no position fix yet.", "Auto-route");
}
void AutoRouteDialog::OnCursorFrom(wxCommandEvent&) { owner_->BeginPick(true); }
void AutoRouteDialog::OnCursorTo(wxCommandEvent&) { owner_->BeginPick(false); }

// Switching units converts what is already typed, so the numbers keep meaning the same thing.
void AutoRouteDialog::OnUnits(wxCommandEvent&) {
    const int now = units_->GetSelection();
    if (now == lastUnits_) return;
    const double k = now == 1 ? 1.0 / kFeetToM : kFeetToM;   // metres to feet, or feet to metres
    for (wxTextCtrl* c : {length_, draft_, clearance_, airDraft_}) {
        double v;
        if (c->GetValue().ToDouble(&v)) c->SetValue(wxString::Format("%.1f", v * k));
    }
    lastUnits_ = now;
}

void AutoRouteDialog::SetRunning(bool running) {
    running_ = running;
    plan_->Enable(!running);
    cancel_->SetLabel(running ? "Cancel" : "Close");
    if (!running) gauge_->SetValue(0);
}

void AutoRouteDialog::OnCancelOrClose(wxCommandEvent&) {
    if (running_) {
        cancel_flag_ = true;
        phase_->SetLabel("Cancelling...");
    } else {
        Close();
    }
}

void AutoRouteDialog::OnClose(wxCloseEvent&) {
    Save();
    cancel_flag_ = true;
    owner_->Closed();
    Destroy();
}

void AutoRouteDialog::OnPlan(wxCommandEvent&) {
    PlanRequest req;
    if (!parsePoint(from_->GetValue(), req.from) || !parsePoint(to_->GetValue(), req.to)) {
        wxMessageBox("Enter both positions as decimal degrees, for example 47.605, -122.360 (south and west are negative).", "Auto-route");
        return;
    }
    double len = 0, draft = 0, clr = 0, air = -1.0;
    if (!length_->GetValue().ToDouble(&len) || len <= 0 || !draft_->GetValue().ToDouble(&draft) || draft < 0 ||
        !clearance_->GetValue().ToDouble(&clr) || clr < 0) {
        wxMessageBox("Vessel length must be more than zero, and draft and clearance zero or more.", "Auto-route");
        return;
    }
    if (!airDraft_->GetValue().Trim().IsEmpty() && (!airDraft_->GetValue().ToDouble(&air) || air <= 0)) {
        wxMessageBox("Air draft must be a positive number, or blank to estimate it from the vessel length.", "Auto-route");
        return;
    }
    if (encDir_->GetPath().IsEmpty()) {
        wxMessageBox("Choose the folder that holds your NOAA S-57 chart cells (an ENC_ROOT folder).", "Auto-route");
        return;
    }
    Save();
    req.encDir = encDir_->GetPath().ToStdString();
    req.lengthM = ToMetres(len);
    req.draftM = ToMetres(draft);
    req.clearanceM = ToMetres(clr);
    req.airDraftM = air > 0 ? ToMetres(air) : -1.0;
    req.underSail = sail_->GetValue();
    req.cellM = suggestedCellM(req.from, req.to);   // fine for a short trip, coarser for a long one so the laptop's memory holds
    req.startName = "Start";
    req.endName = "End";
    req.routeName = wxString::Format("Auto-route %s to %s", from_->GetValue(), to_->GetValue()).ToStdString();

    report_->Clear();
    cancel_flag_ = false;
    SetRunning(true);
    if (worker_.joinable()) worker_.join();
    std::atomic<bool>* cancel = &cancel_flag_;
    worker_ = std::thread([this, req, cancel]() {
        PlanHooks hooks;
        auto lastPost = std::make_shared<std::atomic<long long>>(0);
        hooks.progress = [this, cancel, lastPost](const PlanProgress& p) {
            // At most about ten updates a second: the search reports far more often than the screen needs.
            const long long now = wxGetLocalTimeMillis().GetValue();
            if (now - lastPost->load() >= 100) {
                lastPost->store(now);
                const wxString phase = p.phase;
                const int value = static_cast<int>(p.overall * 1000.0);
                const double frac = p.fraction;
                CallAfter([this, phase, value, frac]() {
                    phase_->SetLabel(wxString::Format("%s (%.0f%%)", phase, frac * 100.0));
                    gauge_->SetValue(std::min(1000, std::max(0, value)));
                });
            }
            return !cancel->load();
        };
        hooks.out = [this](const std::string& s) { CallAfter([this, s]() { report_->AppendText(wxString::FromUTF8(s.c_str())); }); };
        hooks.err = hooks.out;
        PlanResult result = planRoute(req, hooks);
        CallAfter([this, r = std::move(result)]() mutable { Finish(std::move(r)); });
    });
}

void AutoRouteDialog::Finish(PlanResult result) {
    if (worker_.joinable()) worker_.join();
    SetRunning(false);
    report_->Clear();   // drop the chart-by-chart log; the summary below is what matters
    if (result.cancelled) {
        phase_->SetLabel("Cancelled");
        return;
    }
    if (result.status != 0) {
        phase_->SetLabel("No route");
        wxString why = result.failReason;
        if (result.failReason == "disconnected_water") why = "the start and end are in different bodies of water at this depth (or a closed lock, bridge or barrier lies between them)";
        else if (result.failReason == "endpoint_not_in_safe_water") why = "the start or end is not near water deep enough for this draft";
        else if (result.failReason == "endpoint_outside_grid") why = "the start or end is outside the charts";
        else if (result.failReason == "no_charts") why = "no chart cells in that folder cover the area";
        else if (result.failReason == "no_route") why = "no legal route was found between the two points";
        report_->SetValue("No route: " + why + ".");
        return;
    }
    phase_->SetLabel("Done");
    gauge_->SetValue(1000);
    AddToOpenCPN(result);
    wxString text = wxString::Format("Route added to the Route Manager: %.1f nm (straight line %.1f nm), %zu waypoints, %d charts.\n",
                                     result.nm, result.straightNm, result.route.size(), result.chartsUsed);
    if (result.snapStartM > 1.0) text += wxString::Format("The start was moved %s to reach safe water.\n", Length(result.snapStartM));
    if (result.snapEndM > 1.0) text += wxString::Format("The end was moved %s to reach safe water.\n", Length(result.snapEndM));
    if (result.areasCrossed.empty()) {
        text += "\nNo restricted or dangerous charted areas are crossed.";
    } else {
        text += "\nCheck the rules for each of these before you go:\n";
        for (const AreaCrossing& a : result.areasCrossed) {
            text += wxString::Format("  - %s: %.1f nm", wxString::FromUTF8(a.kind.c_str()), a.metres / 1852.0);
            if (!a.text.empty()) text += ": " + wxString::FromUTF8(a.text.c_str());
            text += "\n";
        }
    }
    text += "\nNot for navigation. Check the route against the chart. Depths are at chart datum, with no tide or current.";
    report_->SetValue(text);
}

// The route goes in as a normal OpenCPN route, named so the Route Manager's From and To columns read sensibly.
void AutoRouteDialog::AddToOpenCPN(const PlanResult& result) {
    auto* route = new PlugIn_Route;
    route->m_NameString = wxString::FromUTF8(result.routeName.c_str());
    route->m_StartString = "Start";
    route->m_EndString = "End";
    std::vector<PlugIn_Waypoint*> made;
    for (size_t i = 0; i < result.route.size(); ++i) {
        const wxString name = i == 0 ? wxString("Start") : i + 1 == result.route.size() ? wxString("End") : wxString::Format("WP%03zu", i + 1);
        auto* wp = new PlugIn_Waypoint(result.route[i].lat, result.route[i].lon, "diamond", name);
        route->pWaypointList->Append(wp);
        made.push_back(wp);
    }
    AddPlugInRoute(route, true);   // OpenCPN copies the waypoints, so both the route and the waypoints are ours to free
    delete route;
    for (PlugIn_Waypoint* wp : made) delete wp;
    if (wxWindow* canvas = GetOCPNCanvasWindow()) RequestRefresh(canvas);
}

// ---------------------------------------------------------------------------------------------------------------------------------

// Each step OpenCPN takes while loading the plugin is written to OpenCPN's own log, so a failure to appear in the plugin list can be traced.
extern "C" DECL_EXP opencpn_plugin* create_pi(void* ppimgr) {
    wxLogMessage("open-autoroute plugin: create_pi called");
    return new openautoroute_pi(ppimgr);
}
extern "C" DECL_EXP void destroy_pi(opencpn_plugin* p) {
    wxLogMessage("open-autoroute plugin: destroy_pi called");
    delete p;
}

openautoroute_pi::openautoroute_pi(void* ppimgr) : opencpn_plugin_118(ppimgr) {
    icon_ = makeIcon();
    wxLogMessage("open-autoroute plugin: constructed, icon %s", icon_.IsOk() ? "ok" : "NOT ok");
}
openautoroute_pi::~openautoroute_pi() = default;

int openautoroute_pi::Init() {
    wxLogMessage("open-autoroute plugin: Init");
    toolId_ = InsertPlugInTool("", &icon_, &icon_, wxITEM_NORMAL, "Auto-route", "Plan a route between two points", nullptr, -1, 0, this);
    fromItem_ = AddCanvasContextMenuItem(new wxMenuItem(nullptr, wxID_ANY, "Auto-route from here"), this);
    toItem_ = AddCanvasContextMenuItem(new wxMenuItem(nullptr, wxID_ANY, "Auto-route to here"), this);
    return WANTS_TOOLBAR_CALLBACK | INSTALLS_TOOLBAR_TOOL | INSTALLS_CONTEXTMENU_ITEMS | WANTS_CURSOR_LATLON | WANTS_MOUSE_EVENTS | WANTS_NMEA_EVENTS | WANTS_CONFIG;
}

bool openautoroute_pi::DeInit() {
    wxLogMessage("open-autoroute plugin: DeInit");
    if (dialog_) {
        dialog_->Close();   // saves its settings and destroys itself (which also stops any planning)
        dialog_ = nullptr;
    }
    RemovePlugInTool(toolId_);
    return true;
}

int openautoroute_pi::GetAPIVersionMajor() {
    wxLogMessage("open-autoroute plugin: GetAPIVersionMajor -> %d", API_VERSION_MAJOR);
    return API_VERSION_MAJOR;
}
// Must match the base class this plugin is built on (opencpn_plugin_118), not the newest API the header describes: OpenCPN casts the plugin
// to the class named by this number and calls it incompatible when the cast fails.
int openautoroute_pi::GetAPIVersionMinor() { return 18; }
int openautoroute_pi::GetPlugInVersionMajor() { return 0; }
int openautoroute_pi::GetPlugInVersionMinor() { return 0; }   // pre-release: no version numbers until the first release
wxBitmap* openautoroute_pi::GetPlugInBitmap() {
    wxLogMessage("open-autoroute plugin: GetPlugInBitmap");
    return &icon_;
}
wxString openautoroute_pi::GetCommonName() {
    wxLogMessage("open-autoroute plugin: GetCommonName");
    return "Auto-route";
}
wxString openautoroute_pi::GetShortDescription() { return "Plan a safe route between two points from NOAA S-57 charts"; }
wxString openautoroute_pi::GetLongDescription() {
    return "Plans a route between two points using the open-autoroute engine: depth against your draft, land, obstructions and wrecks, "
           "traffic separation schemes, narrow channels, restricted areas and navigation locks. The route is added to the Route Manager. "
           "A planning aid only; not for navigation. Check every route against the chart.";
}

wxString openautoroute_pi::DefaultEncDir() const {
    const wxArrayString dirs = GetChartDBDirArrayString();
    for (const wxString& d : dirs) {
        if (d.Lower().Contains("enc")) return d;   // a chart folder that looks like ENC_ROOT
    }
    return dirs.IsEmpty() ? wxString() : dirs[0];
}

void openautoroute_pi::ShowDialog() {
    if (!dialog_) dialog_ = new AutoRouteDialog(GetOCPNCanvasWindow(), this);
    dialog_->Show();
    dialog_->Raise();
}

void openautoroute_pi::OnToolbarToolCallback(int) { ShowDialog(); }

void openautoroute_pi::OnContextMenuItemCallback(int id) {
    ShowDialog();
    if (id == fromItem_) dialog_->SetFrom(cursorLat_, cursorLon_);
    else if (id == toItem_) dialog_->SetTo(cursorLat_, cursorLon_);
}

void openautoroute_pi::BeginPick(bool forFrom) {
    if (!dialog_) return;
    picking_ = true;
    pickFrom_ = forFrom;
    dialog_->Hide();   // out of the way, so the whole chart can be clicked
    if (wxWindow* canvas = GetOCPNCanvasWindow()) canvas->SetCursor(wxCursor(wxCURSOR_CROSS));
}

void openautoroute_pi::CancelPick() {
    if (!picking_) return;
    picking_ = false;
    if (wxWindow* canvas = GetOCPNCanvasWindow()) canvas->SetCursor(wxNullCursor);
    if (dialog_) {
        dialog_->Show();
        dialog_->Raise();
    }
}

// While picking, the next left-click on the chart is the position; a right-click cancels. Both are consumed so the chart does not also react.
bool openautoroute_pi::MouseEventHook(wxMouseEvent& event) {
    if (!picking_) return false;
    if (event.LeftDown() || event.LeftUp() || event.LeftDClick()) {
        if (event.LeftUp()) {
            const bool forFrom = pickFrom_;
            const double lat = cursorLat_, lon = cursorLon_;
            CancelPick();
            if (dialog_) {
                if (forFrom) dialog_->SetFrom(lat, lon);
                else dialog_->SetTo(lat, lon);
            }
        }
        return true;
    }
    if (event.RightDown() || event.RightUp()) {
        if (event.RightUp()) CancelPick();
        return true;
    }
    return false;
}

void openautoroute_pi::SetCursorLatLon(double lat, double lon) {
    cursorLat_ = lat;
    cursorLon_ = lon;
}

void openautoroute_pi::SetPositionFix(PlugIn_Position_Fix& pfix) {
    shipLat_ = pfix.Lat;
    shipLon_ = pfix.Lon;
    haveShip_ = true;
}
