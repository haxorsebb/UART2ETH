/**
 * @file html_common.h
 * @brief Common HTML templates and CSS styles
 * 
 * Shared HTML components used across all pages
 */

#ifndef HTML_COMMON_H
#define HTML_COMMON_H

// Common CSS stylesheet
#define HTML_CSS_STYLES \
    "body { font-family: Arial, sans-serif; margin: 40px; background-color: #f5f5f5; }\n" \
    ".container { background-color: white; padding: 30px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); max-width: 1000px; margin: 0 auto; }\n" \
    ".header { color: #2c3e50; border-bottom: 2px solid #3498db; padding-bottom: 10px; margin-bottom: 30px; }\n" \
    ".section { margin-bottom: 25px; padding: 20px; border: 1px solid #ddd; border-radius: 4px; }\n" \
    ".label { font-weight: bold; color: #34495e; }\n" \
    ".value { color: #2980b9; font-family: monospace; }\n" \
    ".status-ok { color: #27ae60; font-weight: bold; }\n" \
    ".status-warning { color: #f39c12; font-weight: bold; }\n" \
    ".status-error { color: #e74c3c; font-weight: bold; }\n" \
    ".nav-links { margin: 20px 0; text-align: center; }\n" \
    ".nav-links a { display: inline-block; margin: 0 10px; padding: 10px 20px; background-color: #95a5a6; color: white; text-decoration: none; border-radius: 4px; }\n" \
    ".nav-links a:hover { background-color: #7f8c8d; }\n" \
    ".nav-links a.active { background-color: #3498db; }\n" \
    ".button { background-color: #3498db; color: white; padding: 12px 24px; border: none; border-radius: 4px; cursor: pointer; font-size: 16px; }\n" \
    ".button:hover { background-color: #2980b9; }\n" \
    ".button-danger { background-color: #e74c3c; }\n" \
    ".button-danger:hover { background-color: #c0392b; }\n" \
    ".message { padding: 15px; border-radius: 4px; margin-bottom: 20px; }\n" \
    ".message-success { background-color: #d5f5e3; border: 1px solid #27ae60; color: #1e8449; }\n" \
    ".message-error { background-color: #fadbd8; border: 1px solid #e74c3c; color: #c0392b; }\n"

// Navigation links
#define HTML_NAV_STATUS \
    "<div class=\"nav-links\">\n" \
    "    <a href=\"/\" class=\"active\">Status</a>\n" \
    "    <a href=\"/config\">Configuration</a>\n" \
    "    <a href=\"/update\">Update</a>\n" \
    "</div>\n"

#define HTML_NAV_CONFIG \
    "<div class=\"nav-links\">\n" \
    "    <a href=\"/\">Status</a>\n" \
    "    <a href=\"/config\" class=\"active\">Configuration</a>\n" \
    "    <a href=\"/update\">Update</a>\n" \
    "</div>\n"

#define HTML_NAV_UPDATE \
    "<div class=\"nav-links\">\n" \
    "    <a href=\"/\">Status</a>\n" \
    "    <a href=\"/config\">Configuration</a>\n" \
    "    <a href=\"/update\" class=\"active\">Update</a>\n" \
    "</div>\n"

#endif // HTML_COMMON_H
