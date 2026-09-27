#pragma once

// OpenCPN plugin: plan a route between two points with the open-autoroute core and add it to OpenCPN's Route Manager.
// The plugin is only a front end. Everything about the sea (charts, depths, rules of the road, restricted areas, locks) is in the core;
// this file adds the buttons, the dialog, a progress bar, and the conversion between the units a person sails in and the core's metres.

#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "ocpn_plugin.h"
#include "openautoroute/planner.hpp"

class AutoRouteDialog;

class openautoroute_pi : public opencpn_plugin_118 {
public:
    explicit openautoroute_pi(void* ppimgr);
    ~openautoroute_pi() override;

    int Init() override;
    bool DeInit() override;

    int GetAPIVersionMajor() override;
    int GetAPIVersionMinor() override;
    int GetPlugInVersionMajor() override;
    int GetPlugInVersionMinor() override;
    wxBitmap* GetPlugInBitmap() override;
    wxString GetCommonName() override;
    wxString GetShortDescription() override;
    wxString GetLongDescription() override;

    void OnToolbarToolCallback(int id) override;
    void OnContextMenuItemCallback(int id) override;
    void SetCursorLatLon(double lat, double lon) override;
    void SetPositionFix(PlugIn_Position_Fix& pfix) override;
    bool MouseEventHook(wxMouseEvent& event) override;

    // Used by the dialog.
    bool HaveShip() const { return haveShip_; }
    double ShipLat() const { return shipLat_; }
    double ShipLon() const { return shipLon_; }
    double CursorLat() const { return cursorLat_; }
    double CursorLon() const { return cursorLon_; }
    std::vector<std::string> ChartFolders() const;
    void Closed() { CancelPick(); dialog_ = nullptr; }

    /// Hide the dialog and let the next left-click on the chart set its "from" or "to" position (a right-click cancels).
    void BeginPick(bool forFrom);
    void CancelPick();

private:
    void ShowDialog();

    wxBitmap icon_;
    AutoRouteDialog* dialog_ = nullptr;
    int toolId_ = 0, fromItem_ = 0, toItem_ = 0;
    double cursorLat_ = 0.0, cursorLon_ = 0.0, shipLat_ = 0.0, shipLon_ = 0.0;
    bool haveShip_ = false;
    bool picking_ = false, pickFrom_ = true;
};
