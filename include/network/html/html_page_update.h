/**
 * @file html_page_update.h
 * @brief Firmware update page HTML template
 * 
 * Template for /update endpoint with firmware upload form
 */

#ifndef HTML_PAGE_UPDATE_H
#define HTML_PAGE_UPDATE_H

#define HTML_UPDATE_PAGE_TEMPLATE \
    "HTTP/1.0 200 OK\r\n" \
    "Content-Type: text/html\r\n" \
    "Connection: close\r\n" \
    "\r\n" \
    "<!DOCTYPE html>\n" \
    "<html>\n" \
    "<head>\n" \
    "    <title>UART2ETH Firmware Update</title>\n" \
    "    <style>\n" \
    HTML_CSS_STYLES \
    "        .upload-area { border: 2px dashed #3498db; padding: 30px; text-align: center; border-radius: 8px; margin: 20px 0; }\n" \
    "        .upload-area:hover { background-color: #ecf0f1; }\n" \
    "        input[type=file] { margin: 15px 0; }\n" \
    "    </style>\n" \
    "</head>\n" \
    "<body>\n" \
    "    <div class=\"container\">\n" \
    "        <div class=\"header\">\n" \
    "            <h1>Firmware Update</h1>\n" \
    "            <p>Upload new firmware and manage device reboot</p>\n" \
    "        </div>\n" \
    "        \n" \
    HTML_NAV_UPDATE \
    "        \n" \
    "%s" /* Message placeholder */ \
    "        \n" \
    "        <div class=\"section\">\n" \
    "            <h3>Upload Firmware</h3>\n" \
    "            <p>Upload a .uf2 firmware file for OTA update. Maximum size: 1024 KB</p>\n" \
    "            <div class=\"upload-area\">\n" \
    "                <form method=\"POST\" action=\"/update\" enctype=\"multipart/form-data\">\n" \
    "                    <p><strong>Select Firmware File:</strong></p>\n" \
    "                    <input type=\"file\" name=\"firmware\" accept=\".uf2\" required>\n" \
    "                    <br>\n" \
    "                    <button type=\"submit\" class=\"button\">Upload & Install</button>\n" \
    "                </form>\n" \
    "            </div>\n" \
    "            <p><em>Note: Device will automatically reboot to apply new firmware after successful upload.</em></p>\n" \
    "        </div>\n" \
    "        \n" \
    "        <div class=\"section\">\n" \
    "            <h3>Device Reboot</h3>\n" \
    "            <p>Manually reboot the device to apply changes or recover from errors.</p>\n" \
    "            <p><strong>Warning:</strong> All active connections will be terminated.</p>\n" \
    "            <form method=\"POST\" action=\"/reboot\">\n" \
    "                <button type=\"submit\" class=\"button button-danger\">Reboot Device</button>\n" \
    "            </form>\n" \
    "        </div>\n" \
    "    </div>\n" \
    "</body>\n" \
    "</html>\n"

#endif // HTML_PAGE_UPDATE_H
