#pragma once

// Shared <head> content for the two web portals (SoundUpload.cpp,
// WifiSetup.cpp) — both otherwise-unstyled plain-HTML pages. Mirrors
// the physical Settings menu's visual language: black-on-white,
// rounded "pill" buttons (matching the selected-value pill drawn on
// the e-paper screen), Google Sans Flex.
//
// The Google Fonts <link> only actually loads when the viewing device
// has real internet access — true for SoundUpload's portal (served
// once already on the home WiFi), but NOT for WifiSetup's AP-mode
// captive portal, which by definition runs before any internet
// connection exists yet. That's fine: the font-family fallback list
// below just silently takes over in that case, no error, no broken
// layout — only the exact typeface differs.
constexpr const char *kWebPortalHead =
    "<meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<link rel='preconnect' href='https://fonts.googleapis.com'>"
    "<link rel='preconnect' href='https://fonts.gstatic.com' crossorigin>"
    "<link href='https://fonts.googleapis.com/css2?family=Google+Sans+Flex:wght@400;500;600;700&display=swap' "
    "rel='stylesheet'>"
    "<style>"
    "body{font-family:'Google Sans Flex',-apple-system,'Segoe UI',Roboto,sans-serif;"
    "background:#fff;color:#000;max-width:480px;margin:0 auto;padding:28px 20px;line-height:1.5}"
    "h3{font-weight:600;font-size:22px;margin:0 0 20px;letter-spacing:-0.01em}"
    "p{margin:0 0 16px}"
    "ul{list-style:none;margin:0 0 20px;padding:0}"
    "li{display:flex;align-items:center;justify-content:space-between;"
    "padding:12px 4px;border-bottom:1px solid #e5e5e5;gap:8px;flex-wrap:wrap}"
    "li:last-child{border-bottom:none}"
    "form{margin:0 0 20px}"
    "input[type=text],input[type=password]{font-family:inherit;font-size:16px;padding:10px 14px;"
    "border:1px solid #ccc;border-radius:20px;width:100%;box-sizing:border-box;margin-bottom:12px}"
    "input[type=file]{font-family:inherit;margin-bottom:12px}"
    "input[type=submit],button{font-family:inherit;font-weight:500;font-size:15px;"
    "background:#000;color:#fff;border:none;border-radius:20px;padding:10px 20px;cursor:pointer}"
    "input[type=submit]:disabled{background:#ccc;cursor:default}"
    "button[type=button]{background:#fff;color:#000;border:1px solid #000;padding:6px 14px;font-size:13px}"
    "a{color:#000;font-weight:500}"
    "</style>";
