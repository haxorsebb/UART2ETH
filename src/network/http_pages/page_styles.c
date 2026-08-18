/**
 * @file page_styles.c
 * @brief HTTP stylesheet generation implementation
 * 
 * Generates common CSS styles for the web management interface.
 * 
 * Documentation Reference:
 * - ADR-018: HTTP Server Modularization
 */

#include "network/http_pages/page_styles.h"
#include <stdio.h>
#include <string.h>

/**
 * @brief Generate CSS stylesheet response
 * 
 * Creates a complete HTTP response with minified CSS containing common styles
 * for all web interface pages. Minified to conserve memory and reduce
 * transmission size.
 * 
 * @param buffer Output buffer for HTTP response
 * @param buffer_size Size of output buffer in bytes
 * 
 * Documentation Reference: ADR-018
 */
void http_generate_stylesheet(char* buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) {
        return;
    }
    
    // Common CSS styles for all pages - minified to save space
    snprintf(buffer, buffer_size,
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: text/css\r\n"
        "Connection: close\r\n"
        "\r\n"
        "body{font-family:Arial,sans-serif;margin:40px;background-color:#f5f5f5}"
        ".container{background-color:white;padding:30px;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1);max-width:900px}"
        ".header{color:#2c3e50;border-bottom:2px solid #3498db;padding-bottom:10px;margin-bottom:30px}"
        ".header.warning{border-bottom-color:#e67e22}"
        ".section{margin-bottom:25px;padding:20px;border:1px solid #ddd;border-radius:4px}"
        ".section h3{margin-top:0;color:#2c3e50}"
        ".label{font-weight:bold;color:#34495e}"
        ".value{color:#2980b9;font-family:monospace}"
        ".status-ok{color:#27ae60;font-weight:bold}"
        ".port-table{border-collapse:collapse;width:100%%}"
        ".port-table th,.port-table td{border:1px solid #ddd;padding:8px;text-align:left}"
        ".port-table th{background-color:#3498db;color:white}"
        ".nav-links{margin:20px 0;text-align:center}"
        ".nav-links a{display:inline-block;margin:0 10px;padding:10px 20px;background-color:#95a5a6;color:white;text-decoration:none;border-radius:4px}"
        ".nav-links a:hover{background-color:#7f8c8d}"
        ".nav-links a.active{background-color:#3498db}"
        ".mode-badge{display:inline-block;padding:4px 12px;background-color:#9b59b6;color:white;border-radius:12px;font-size:12px;margin-left:10px}"
        ".warning-badge{display:inline-block;padding:6px 15px;background-color:#e67e22;color:white;border-radius:4px;font-size:14px;font-weight:bold;margin-left:10px}"
        ".factory-badge{display:inline-block;padding:6px 15px;background-color:#e74c3c;color:white;border-radius:4px;font-size:14px;font-weight:bold;margin-left:10px;animation:blink 1s infinite}"
        "@keyframes blink{0%%,100%%{opacity:1}50%%{opacity:0.5}}"
        ".form-group{margin-bottom:15px}"
        ".form-group label{display:block;margin-bottom:5px;font-weight:bold;color:#34495e}"
        ".form-group input,.form-group select{width:100%%;padding:8px;border:1px solid #ddd;border-radius:4px;box-sizing:border-box}"
        ".form-group small{display:block;margin-top:3px;color:#7f8c8d;font-size:12px}"
        ".form-row{display:flex;gap:15px}"
        ".form-row .form-group{flex:1}"
        ".checkbox-group{display:flex;align-items:center}"
        ".checkbox-group input[type=checkbox]{width:auto;margin-right:10px}"
        ".button{background-color:#3498db;color:white;padding:12px 24px;border:none;border-radius:4px;cursor:pointer;font-size:16px;font-weight:bold}"
        ".button:hover{background-color:#2980b9}"
        ".button-success{background-color:#27ae60}"
        ".button-success:hover{background-color:#229954}"
        ".button-danger{background-color:#e74c3c}"
        ".button-danger:hover{background-color:#c0392b}"
        ".button-secondary{background-color:#95a5a6}"
        ".button-secondary:hover{background-color:#7f8c8d}"
        ".current-status{background-color:#ecf0f1;padding:10px;border-radius:4px;margin-bottom:15px}"
        ".current-factory{padding:15px;border-radius:4px;margin-bottom:20px;border-left:4px solid}"
        ".current-factory.valid{background-color:#d5f4e6;border-color:#27ae60}"
        ".current-factory.invalid{background-color:#fadbd8;border-color:#e74c3c}"
        ".current-factory h4{margin-top:0;color:#2c3e50}"
        ".uart-row{display:flex;align-items:center;gap:15px;margin-bottom:15px}"
        ".uart-row>*{flex:1}"
        ".preview-box{background-color:#ecf0f1;padding:10px;border-radius:4px;margin-top:5px;font-family:monospace;font-size:14px;font-weight:bold}"
        ".error{color:#e74c3c}"
        ".success{color:#27ae60}\r\n"
    );
    
    /* printf("HTTP: Generated CSS stylesheet (%zu bytes)\n", strlen(buffer)); */
}
