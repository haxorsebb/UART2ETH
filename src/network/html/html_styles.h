/**
 * @file html_styles.h
 * @brief CSS stylesheet for UART2ETH web interface
 * 
 * Common styles used across all HTML pages
 */

#ifndef HTML_STYLES_H
#define HTML_STYLES_H

#define HTML_CSS_STYLESHEET \
    "body { font-family: Arial, sans-serif; margin: 40px; background-color: #f5f5f5; }\n" \
    ".container { background-color: white; padding: 30px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); max-width: 1000px; margin: 0 auto; }\n" \
    ".header { color: #2c3e50; border-bottom: 2px solid #3498db; padding-bottom: 10px; margin-bottom: 30px; }\n" \
    ".section { margin-bottom: 25px; padding: 20px; border: 1px solid #ddd; border-radius: 4px; }\n" \
    ".section h3 { margin-top: 0; color: #2c3e50; }\n" \
    ".label { font-weight: bold; color: #34495e; }\n" \
    ".value { color: #2980b9; font-family: monospace; }\n" \
    ".status-ok { color: #27ae60; font-weight: bold; }\n" \
    ".status-warning { color: #f39c12; font-weight: bold; }\n" \
    ".status-error { color: #e74c3c; font-weight: bold; }\n" \
    ".port-table { border-collapse: collapse; width: 100%%; }\n" \
    ".port-table th, .port-table td { border: 1px solid #ddd; padding: 8px; text-align: left; }\n" \
    ".port-table th { background-color: #3498db; color: white; }\n" \
    ".nav-links { margin: 20px 0; text-align: center; }\n" \
    ".nav-links a { display: inline-block; margin: 0 10px; padding: 10px 20px; background-color: #95a5a6; color: white; text-decoration: none; border-radius: 4px; }\n" \
    ".nav-links a:hover { background-color: #7f8c8d; }\n" \
    ".nav-links a.active { background-color: #3498db; }\n" \
    ".mode-badge { display: inline-block; padding: 4px 12px; background-color: #9b59b6; color: white; border-radius: 12px; font-size: 12px; margin-left: 10px; }\n" \
    ".button { background-color: #3498db; color: white; padding: 12px 24px; border: none; border-radius: 4px; cursor: pointer; font-size: 16px; font-weight: bold; }\n" \
    ".button:hover { background-color: #2980b9; }\n" \
    ".button-secondary { background-color: #95a5a6; }\n" \
    ".button-secondary:hover { background-color: #7f8c8d; }\n" \
    ".button-danger { background-color: #e74c3c; }\n" \
    ".button-danger:hover { background-color: #c0392b; }\n" \
    ".button-success { background-color: #27ae60; }\n" \
    ".button-success:hover { background-color: #229954; }\n" \
    ".form-group { margin-bottom: 15px; }\n" \
    ".form-group label { display: block; margin-bottom: 5px; font-weight: bold; color: #34495e; }\n" \
    ".form-group input, .form-group select { width: 100%%; padding: 8px; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box; }\n" \
    ".checkbox-group { display: flex; align-items: center; }\n" \
    ".checkbox-group input[type=checkbox] { width: auto; margin-right: 10px; }\n" \
    ".message { padding: 15px; border-radius: 4px; margin-bottom: 20px; }\n" \
    ".message-info { background-color: #d5f5e3; border: 1px solid #27ae60; color: #27ae60; }\n" \
    ".message-error { background-color: #fadbd8; border: 1px solid #e74c3c; color: #e74c3c; }\n" \
    ".progress-bar { width: 100%%; height: 30px; background-color: #ecf0f1; border-radius: 4px; overflow: hidden; margin: 10px 0; }\n" \
    ".progress-fill { height: 100%%; background-color: #3498db; transition: width 0.3s ease; }\n"

#endif // HTML_STYLES_H
