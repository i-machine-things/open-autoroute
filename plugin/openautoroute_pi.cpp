#include "openautoroute_pi.h"

#include <wx/wx.h>
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

// OpenCPN's route interface published in 5.14 (API 1.21): reads and writes a route with its colour, visibility and full waypoints.
std::shared_ptr<HostApi121> hostApi() {
    std::shared_ptr<HostApi> api = GetHostApi();
    return std::dynamic_pointer_cast<HostApi121>(api);
}

template <typename F>
void forEachWaypoint(const HostApi121::Route& route, F f) {
    if (!route.pWaypointList) return;
    for (auto* node = route.pWaypointList->GetFirst(); node; node = node->GetNext()) f(*node->GetData());
}

std::vector<LatLon> routePoints(const HostApi121::Route& route) {
    std::vector<LatLon> pts;
    forEachWaypoint(route, [&](const PlugIn_Waypoint_ExV2& wp) { pts.push_back({wp.m_lat, wp.m_lon}); });
    return pts;
}

bool samePoints(const std::vector<LatLon>& a, const std::vector<LatLon>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::fabs(a[i].lat - b[i].lat) > 1e-9 || std::fabs(a[i].lon - b[i].lon) > 1e-9) return false;
    }
    return true;
}

// A copy of a waypoint with an empty GUID: OpenCPN gives it a new one, so the copy never shares a point with another route.
PlugIn_Waypoint_ExV2* freshCopy(const PlugIn_Waypoint_ExV2& wp) {
    auto* copy = new PlugIn_Waypoint_ExV2(wp);
    copy->m_GUID = "";
    return copy;
}

// The route's settings (name, colour, style, planned speed...) without its waypoints or GUID.
std::unique_ptr<HostApi121::Route> sameSettings(const HostApi121::Route& src) {
    auto dst = std::make_unique<HostApi121::Route>();
    dst->m_NameString = src.m_NameString;
    dst->m_StartString = src.m_StartString;
    dst->m_EndString = src.m_EndString;
    dst->m_isVisible = src.m_isVisible;
    dst->m_Description = src.m_Description;
    dst->m_PlannedSpeed = src.m_PlannedSpeed;
    dst->m_Colour = src.m_Colour;
    dst->m_style = src.m_style;
    dst->m_PlannedDeparture = src.m_PlannedDeparture;
    dst->m_TimeDisplayFormat = src.m_TimeDisplayFormat;
    return dst;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------------------

class AutoRouteDialog : public wxDialog {
public:
    AutoRouteDialog(wxWindow* parent, openautoroute_pi* owner);
    ~AutoRouteDialog() override;

    /// Stop everything the dialog has running and save its settings, synchronously. The plugin calls this from DeInit and then deletes the
    /// dialog before returning, because OpenCPN unloads the plugin's library straight after: no thread or queued callback may outlive that.
    void Shutdown();

    void SetFrom(double lat, double lon) { from_->SetValue(pointText(lat, lon)); }
    void SetTo(double lat, double lon) { to_->SetValue(pointText(lat, lon)); }

    /// Re-plan an existing route through its own waypoints (`check` false), or score it as drawn and change nothing (`check` true).
    void RunOnRoute(const wxString& guid, bool check);

private:
    enum class Job { Plan, Replan, Check };

    void OnPlan(wxCommandEvent&);
    bool ReadVessel(PlanRequest& req);
    void Start(const PlanRequest& req, Job job);
    wxString NoRouteText(const PlanResult& result) const;
    wxString RouteSummary(const PlanResult& result) const;
    void FinishReplan(const PlanResult& result);
    void FinishCheck(const PlanResult& result);
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
    wxGauge* gauge_;
    wxStaticText* phase_;
    wxButton *plan_, *cancel_;
    std::thread worker_;
    std::atomic<bool> cancel_flag_{false};
    bool running_ = false;
    int lastUnits_ = 0;
    Job job_ = Job::Plan;
    // The route a Replan or Check started from, as it was when the job started (OpenCPN may change it while the plan runs).
    wxString routeGuid_;
    std::vector<LatLon> routePts_;
    double cellM_ = 30.0;
};

AutoRouteDialog::AutoRouteDialog(wxWindow* parent, openautoroute_pi* owner)
    : wxDialog(parent, wxID_ANY, "Auto-route", wxDefaultPosition, wxSize(520, 640), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER), owner_(owner) {
    wxConfigBase* cfg = GetOCPNConfigObject();
    wxString from, to, len = "12", draft = "1.5", clr = "1.0", air;
    long units = 0, sail = 0;
    if (cfg) {
        cfg->SetPath("/PlugIns/OpenAutoRoute");
        cfg->Read("From", &from);
        cfg->Read("To", &to);
        cfg->Read("Units", &units, 0L);
        cfg->Read("Sail", &sail, 0L);
        cfg->Read("Length", &len);
        cfg->Read("Draft", &draft);
        cfg->Read("Clearance", &clr);
        cfg->Read("AirDraft", &air);
        cfg->SetPath("/");
    }

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

    // Charts come from OpenCPN's own chart folders: nothing to configure here.
    top->Add(label("Charts: S-57 (ENC) cells in OpenCPN's chart folders"), 0, wxLEFT | wxRIGHT | wxTOP, 8);

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

// Closing only hides the dialog (planning is cancelled); the plugin keeps it and deletes it in DeInit, while its code is still loaded.
void AutoRouteDialog::OnClose(wxCloseEvent&) {
    Save();
    cancel_flag_ = true;
    owner_->CancelPick();
    Hide();
}

void AutoRouteDialog::Shutdown() {
    Save();
    cancel_flag_ = true;
    if (worker_.joinable()) worker_.join();   // the planner checks the flag often, so this is quick
    owner_->CancelPick();
}

void AutoRouteDialog::OnPlan(wxCommandEvent&) {
    PlanRequest req;
    if (!parsePoint(from_->GetValue(), req.from) || !parsePoint(to_->GetValue(), req.to)) {
        wxMessageBox("Enter both positions as decimal degrees, for example 47.605, -122.360 (south and west are negative).", "Auto-route");
        return;
    }
    if (!ReadVessel(req)) return;
    req.cellM = suggestedCellM(req.from, req.to);   // fine for a short trip, coarser for a long one so the laptop's memory holds
    req.startName = "Start";
    req.endName = "End";
    req.routeName = wxString::Format("Auto-route %s to %s", from_->GetValue(), to_->GetValue()).ToStdString();
    Start(req, Job::Plan);
}

// The vessel and the charts, from the dialog and OpenCPN; false (after telling the user why) if something is missing or wrong.
bool AutoRouteDialog::ReadVessel(PlanRequest& req) {
    double len = 0, draft = 0, clr = 0, air = -1.0;
    if (!length_->GetValue().ToDouble(&len) || len <= 0 || !draft_->GetValue().ToDouble(&draft) || draft < 0 ||
        !clearance_->GetValue().ToDouble(&clr) || clr < 0) {
        wxMessageBox("Vessel length must be more than zero, and draft and clearance zero or more.", "Auto-route");
        return false;
    }
    if (!airDraft_->GetValue().Trim().IsEmpty() && (!airDraft_->GetValue().ToDouble(&air) || air <= 0)) {
        wxMessageBox("Air draft must be a positive number, or blank to estimate it from the vessel length.", "Auto-route");
        return false;
    }
    const std::vector<std::string> chartDirs = owner_->ChartFolders();
    if (chartDirs.empty()) {
        wxMessageBox("OpenCPN has no chart folders yet. Add the folder with your NOAA S-57 (ENC) cells in Options, Charts.", "Auto-route");
        return false;
    }
    Save();
    req.encDirs = chartDirs;
    req.lengthM = ToMetres(len);
    req.draftM = ToMetres(draft);
    req.clearanceM = ToMetres(clr);
    req.airDraftM = air > 0 ? ToMetres(air) : -1.0;
    req.underSail = sail_->GetValue();
    return true;
}

void AutoRouteDialog::RunOnRoute(const wxString& guid, bool check) {
    if (running_) {
        wxMessageBox("A plan is already running. Wait for it or cancel it first.", "Auto-route");
        return;
    }
    std::shared_ptr<HostApi121> api = hostApi();
    std::unique_ptr<HostApi121::Route> route = api ? api->GetRoute(guid) : nullptr;
    if (!route) {
        wxMessageBox("OpenCPN could not find that route.", "Auto-route");
        return;
    }
    std::vector<LatLon> pts = routePoints(*route);
    if (pts.size() < 2) {
        wxMessageBox("The route needs at least two waypoints.", "Auto-route");
        return;
    }
    PlanRequest req;
    if (!ReadVessel(req)) return;
    routeGuid_ = guid;
    routePts_ = pts;
    SetFrom(pts.front().lat, pts.front().lon);
    SetTo(pts.back().lat, pts.back().lon);
    if (check) {
        req.evalRoute = pts;
    } else {
        req.from = pts.front();
        req.to = pts.back();
        req.via.assign(pts.begin() + 1, pts.end() - 1);
    }
    req.cellM = suggestedCellM(pts);
    cellM_ = req.cellM;
    req.routeName = route->m_NameString.ToStdString();
    Start(req, check ? Job::Check : Job::Replan);
}

void AutoRouteDialog::Start(const PlanRequest& req, Job job) {
    job_ = job;
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
        report_->SetValue(NoRouteText(result));
        return;
    }
    phase_->SetLabel("Done");
    gauge_->SetValue(1000);
    if (job_ == Job::Replan) return FinishReplan(result);
    if (job_ == Job::Check) return FinishCheck(result);
    AddToOpenCPN(result);
    report_->SetValue("Route added to the Route Manager: " + RouteSummary(result));
}

wxString AutoRouteDialog::NoRouteText(const PlanResult& result) const {
    wxString why = result.failReason;
    if (result.failReason == "disconnected_water") why = "the points are in different bodies of water at this depth (or a closed lock, bridge or barrier lies between them)";
    else if (result.failReason == "endpoint_not_in_safe_water") why = "a point is not near water deep enough for this draft";
    else if (result.failReason == "endpoint_outside_grid") why = "a point is outside the charts";
    else if (result.failReason == "no_charts") why = "no chart cells in that folder cover the area";
    else if (result.failReason == "no_route") why = "no legal route was found between the points";
    wxString text = "No route: " + why + ".";
    if (result.failedLeg >= 0 && routePts_.size() > 2) {
        text += wxString::Format(" The leg with no route is from waypoint %d to waypoint %d.", result.failedLeg + 1, result.failedLeg + 2);
    }
    if (job_ == Job::Replan) text += "\n\nYour route was not changed.";
    return text;
}

// Length, moved points and what the route crosses, with the same warning every plan carries.
wxString AutoRouteDialog::RouteSummary(const PlanResult& result) const {
    wxString text = wxString::Format("%.1f nm (straight line %.1f nm), %zu waypoints, %d charts.\n", result.nm, result.straightNm,
                                     result.route.size(), result.chartsUsed);
    if (result.snapStartM > 1.0) text += wxString::Format("The start was moved %s to reach safe water.\n", Length(result.snapStartM));
    for (size_t i = 0; i < result.snapViaM.size(); ++i) {
        if (result.snapViaM[i] > cellM_) {
            text += wxString::Format("Waypoint %zu was moved %s to reach safe water.\n", i + 2, Length(result.snapViaM[i]));
        }
    }
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
    text += "\nUse at your own risk. Not for navigation: check every leg against the chart and notices to mariners. Depths are at chart datum, with no tide or current. This plugin is not part of OpenCPN.";
    return text;
}

// Replace the route with the plan, keeping the user's own waypoints (name, symbol, description) at the points the plan passes through.
// The original is kept as a hidden copy, because OpenCPN has no undo. If the route changed or went away while the plan ran, or it is
// the route being navigated and the user says no, the original is left alone and the plan is added beside it in green instead.
void AutoRouteDialog::FinishReplan(const PlanResult& result) {
    std::shared_ptr<HostApi121> api = hostApi();
    std::unique_ptr<HostApi121::Route> orig = api ? api->GetRoute(routeGuid_) : nullptr;
    const bool unchanged = orig && samePoints(routePoints(*orig), routePts_) && result.pointIndex.size() == routePts_.size();

    std::vector<const PlugIn_Waypoint_ExV2*> own;
    if (orig) forEachWaypoint(*orig, [&](const PlugIn_Waypoint_ExV2& wp) { own.push_back(&wp); });
    const auto build = [&](std::unique_ptr<HostApi121::Route> route) {
        size_t next = 0;   // the next of the user's waypoints to place
        for (size_t i = 0; i < result.route.size(); ++i) {
            if (unchanged && next < result.pointIndex.size() && result.pointIndex[next] == i) {
                PlugIn_Waypoint_ExV2* wp = freshCopy(*own[next]);
                // A point already in safe water keeps its exact position; one that had to move goes where the plan put it.
                const double moved = next == 0 ? result.snapStartM : next + 1 == routePts_.size() ? result.snapEndM : result.snapViaM[next - 1];
                if (moved > cellM_) {
                    wp->m_lat = result.route[i].lat;
                    wp->m_lon = result.route[i].lon;
                }
                route->pWaypointList->Append(wp);
                ++next;
            } else {
                route->pWaypointList->Append(new PlugIn_Waypoint_ExV2(result.route[i].lat, result.route[i].lon, "diamond", ""));
            }
        }
        return route;
    };
    const auto addBeside = [&](const wxString& why) {
        std::unique_ptr<HostApi121::Route> route = orig ? sameSettings(*orig) : std::make_unique<HostApi121::Route>();
        route->m_NameString = (orig ? orig->m_NameString : wxString::FromUTF8(result.routeName.c_str())) + " (auto-route)";
        route->m_isVisible = true;
        route->m_Colour = "Green";
        route = build(std::move(route));
        if (!api || !api->AddRoute(route.get(), true)) {
            report_->SetValue("OpenCPN did not accept the new route.");
            return;
        }
        report_->SetValue(why + "\n\nAdded as a new route in green: " + RouteSummary(result));
    };

    if (!api) {
        report_->SetValue("This OpenCPN does not offer the route interface the plugin needs (OpenCPN 5.14 or later).");
        return;
    }
    if (!unchanged) {
        addBeside("The route was changed or deleted while the plan ran, so it was left as it is.");
        if (wxWindow* canvas = GetOCPNCanvasWindow()) RequestRefresh(canvas);
        return;
    }
    const bool active = api->IsRouteActive(routeGuid_);
    if (active && wxMessageBox("You are navigating this route. Replace it with the auto-route?\n\n"
                               "Yes: the route is replaced and navigation restarts on it at the best next waypoint.\n"
                               "No: your route is left as it is, and the auto-route is added beside it in green.",
                               "Auto-route", wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, this) != wxYES) {
        addBeside("You are navigating this route, so it was left as it is.");
        if (wxWindow* canvas = GetOCPNCanvasWindow()) RequestRefresh(canvas);
        return;
    }

    // The hidden copy first, so the original is safe before anything is replaced.
    std::unique_ptr<HostApi121::Route> backup = sameSettings(*orig);
    backup->m_NameString = orig->m_NameString + " (before auto-route)";
    backup->m_isVisible = false;
    for (const PlugIn_Waypoint_ExV2* wp : own) backup->pWaypointList->Append(freshCopy(*wp));
    if (!api->AddRoute(backup.get(), true)) {
        report_->SetValue("OpenCPN did not accept the backup copy, so your route was not changed.");
        return;
    }
    std::unique_ptr<HostApi121::Route> route = build(sameSettings(*orig));
    route->m_GUID = routeGuid_;
    if (!api->UpdateRoute(route.get())) {
        report_->SetValue("OpenCPN did not accept the new route. Your route is kept as \"" + backup->m_NameString + "\" (hidden, in the Route Manager).");
        return;
    }
    if (active) api->ActivateRoutePI(routeGuid_, true);
    if (wxWindow* canvas = GetOCPNCanvasWindow()) RequestRefresh(canvas);
    report_->SetValue("Route replaced: " + RouteSummary(result) + "\n\nThe original is kept, hidden, as \"" + backup->m_NameString +
                      "\" in the Route Manager.");
}

// The route as drawn, scored against the same chart rules the planner uses. Nothing in OpenCPN is changed.
void AutoRouteDialog::FinishCheck(const PlanResult& result) {
    wxString text = wxString::FromUTF8(result.checkReport.c_str());
    text += wxString::Format("\nChecked on a %s grid: a hazard smaller than that between two samples can be missed. A second opinion, not a guarantee.",
                             Length(cellM_));
    text += "\nUse at your own risk. Not for navigation: check every leg against the chart and notices to mariners. Depths are at chart datum, with no tide or current. This plugin is not part of OpenCPN.";
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

openautoroute_pi::openautoroute_pi(void* ppimgr) : opencpn_plugin_120(ppimgr) {
    icon_ = makeIcon();
    wxLogMessage("open-autoroute plugin: constructed, icon %s", icon_.IsOk() ? "ok" : "NOT ok");
}
openautoroute_pi::~openautoroute_pi() = default;

int openautoroute_pi::Init() {
    wxLogMessage("open-autoroute plugin: Init");
    toolId_ = InsertPlugInTool("", &icon_, &icon_, wxITEM_NORMAL, "Auto-route", "Plan a route between two points", nullptr, -1, 0, this);
    fromItem_ = AddCanvasContextMenuItem(new wxMenuItem(nullptr, wxID_ANY, "Auto-route from here"), this);
    toItem_ = AddCanvasContextMenuItem(new wxMenuItem(nullptr, wxID_ANY, "Auto-route to here"), this);
    // On a route's own right-click menu; OpenCPN tells OnContextMenuItemCallbackExt which route was clicked.
    replanItem_ = AddCanvasContextMenuItemExt(new wxMenuItem(nullptr, wxID_ANY, "Auto-route this route"), this, "Route");
    checkItem_ = AddCanvasContextMenuItemExt(new wxMenuItem(nullptr, wxID_ANY, "Check this route"), this, "Route");
    return WANTS_TOOLBAR_CALLBACK | INSTALLS_TOOLBAR_TOOL | INSTALLS_CONTEXTMENU_ITEMS | WANTS_CURSOR_LATLON | WANTS_MOUSE_EVENTS | WANTS_NMEA_EVENTS | WANTS_CONFIG;
}

bool openautoroute_pi::DeInit() {
    wxLogMessage("open-autoroute plugin: DeInit");
    if (dialog_) {
        AutoRouteDialog* d = dialog_;
        dialog_ = nullptr;
        d->Shutdown();   // stop the worker and save, then delete now: the library is unloaded as soon as DeInit returns
        delete d;        // (also drops any callbacks still queued for it)
    }
    RemovePlugInTool(toolId_);
    return true;
}

int openautoroute_pi::GetAPIVersionMajor() {
    wxLogMessage("open-autoroute plugin: GetAPIVersionMajor -> %d", API_VERSION_MAJOR);
    return API_VERSION_MAJOR;
}
// Must match the base class this plugin is built on (opencpn_plugin_120), not the newest API the header describes: OpenCPN casts the plugin
// to the class named by this number and calls it incompatible when the cast fails.
int openautoroute_pi::GetAPIVersionMinor() { return 20; }
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
           "Right-click an existing route to re-plan it through its own waypoints, or to check it against the same rules. "
           "A planning aid only, used at your own risk; not for navigation. This plugin is not part of OpenCPN and is not supported by its developers. Check every route against the chart.";
}

// OpenCPN's own chart folders, as set in Options, Charts. The planner searches them (and everything below) for S-57 cells.
std::vector<std::string> openautoroute_pi::ChartFolders() const {
    std::vector<std::string> out;
    for (const wxString& d : GetChartDBDirArrayString()) {
        std::string dir = d.ToStdString();
        const size_t caret = dir.find('^');   // OpenCPN stores a folder as "path^flags" in its config
        if (caret != std::string::npos) dir.resize(caret);
        if (!dir.empty()) out.push_back(dir);
    }
    return out;
}

void openautoroute_pi::ShowDialog() {
    if (!dialog_) dialog_ = new AutoRouteDialog(GetOCPNCanvasWindow(), this);
    dialog_->Show();   // a closed dialog is only hidden, so this brings it back with its settings
    dialog_->Raise();
}

void openautoroute_pi::OnToolbarToolCallback(int) { ShowDialog(); }

void openautoroute_pi::OnContextMenuItemCallback(int id) {
    ShowDialog();
    if (id == fromItem_) dialog_->SetFrom(cursorLat_, cursorLon_);
    else if (id == toItem_) dialog_->SetTo(cursorLat_, cursorLon_);
}

void openautoroute_pi::OnContextMenuItemCallbackExt(int id, std::string obj_ident, std::string obj_type, double, double) {
    if (obj_type != "Route" || obj_ident.empty() || (id != replanItem_ && id != checkItem_)) return;
    ShowDialog();
    dialog_->RunOnRoute(wxString::FromUTF8(obj_ident.c_str()), id == checkItem_);
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
