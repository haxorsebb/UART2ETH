# HTTP Server Refactoring - Continuation Prompt (Phase 4)

## Current Status (as of commit a6b877d)

You are continuing the HTTP server refactoring work on the UART2ETH project. **Phases 1, 2, and 3 are NOW COMPLETE** and committed on branch `feature/http-server-refactoring`.

### What's Been Completed - Phases 1-3 ✅

**Branch:** `feature/http-server-refactoring` (based on main commit 91cb9c6)

**Commits on this branch:**
1. `16172ee` - ADR-018 documentation created
2. `3b36d73` - Page modules extracted (5 page modules)
3. `27052aa` - Factory defaults page added
4. `743d8a9` - Header files moved to include directory
5. `f8f48c6` - **Phase 1 integration completed** (page modules fully working)
6. `cc1b52f` - **Phase 2 completed** (authentication module extracted)
7. `a6b877d` - **Phase 3 completed** (form handling module extracted) ← **YOU ARE HERE**

**Current Metrics:**
```
Original http_server.c:  2,349 lines
After Phase 1:           1,283 lines (-1,066 lines, 45% reduction)
After Phase 2:           1,114 lines (-169 lines)
After Phase 3:             826 lines (-288 lines)
Total reduction:                    (-1,523 lines, 65% reduction!)
Target:                   ~500 lines
Remaining to remove:      ~326 lines
```

**Modules Created:**
```
Phase 1: Page Generation (5 modules, 1,090 total lines)
├── src/network/http_pages/page_device.c (230 lines)
├── src/network/http_pages/page_config.c (250 lines)
├── src/network/http_pages/page_update.c (130 lines)
├── src/network/http_pages/page_styles.c (100 lines)
└── src/network/http_pages/page_factory.c (380 lines, conditional)

Phase 2: Authentication (231 lines)
└── src/network/http_auth.c

Phase 3: Form Handling (446 lines)
└── src/network/http_forms.c

Total: 7 modules, 1,767 lines of well-organized code
```

**Build Status:** ✅ Clean build, all modules compiling successfully

---

## What Remains - Phases 4-5 (Per ADR-018)

### Phase 4: Extract Request Routing (~200 lines) - NEXT UP ⬅️ START HERE

**Goal:** Create a clean route registration and dispatch system

**Current Problem:**
The `http_connection_recv_callback()` function in http_server.c contains a massive if-else chain for routing requests (lines ~450-750). This makes it hard to:
- Add new routes
- See what endpoints exist
- Maintain route logic
- Test routing independently

**Solution:**
Create a route table with handler registration system.

---

## Phase 4 Detailed Implementation Guide

### Step 1: Analyze Current Routing Code

First, understand the current routing in http_server.c:

```bash
# Find the routing section
grep -n "strstr(request_buffer" src/network/http_server.c

# You'll see routes like:
# - POST /change_password
# - POST /factory (conditional)
# - POST /reboot
# - POST /update
# - POST / (config update)
# - GET /styles.css
# - GET /factory (conditional)
# - GET /config
# - GET /update
# - GET / (default)
```

**Current routing pattern:**
```c
if (request_type == HTTP_POST) {
    if (strstr(request_buffer, "POST /change_password") != NULL) {
        // handler code inline
    }
    else if (strstr(request_buffer, "POST /factory") != NULL) {
        // handler code inline
    }
    // ... more routes
}
else if (strstr(request_buffer, "GET /styles.css") != NULL) {
    // handler code inline
}
// ... more routes
```

### Step 2: Create http_router.h Header

**File:** `include/network/http_router.h`

**API Design (from ADR-018):**
```c
#ifndef HTTP_ROUTER_H
#define HTTP_ROUTER_H

#include <stdbool.h>
#include <stddef.h>

// Forward declarations
typedef struct http_connection http_connection_t;

/**
 * @brief HTTP request method types
 */
typedef enum {
    HTTP_METHOD_GET,
    HTTP_METHOD_POST,
    HTTP_METHOD_UNKNOWN
} http_method_t;

/**
 * @brief Route handler function type
 * 
 * Handler functions receive the connection and request buffer.
 * They are responsible for:
 * - Parsing request data
 * - Calling appropriate business logic
 * - Sending response via http_send_response()
 * 
 * @param conn HTTP connection
 * @param request_buffer Full HTTP request string
 * @param buffer_len Length of request buffer
 */
typedef void (*http_route_handler_t)(http_connection_t* conn, 
                                      const char* request_buffer, 
                                      size_t buffer_len);

/**
 * @brief Initialize the HTTP router
 * 
 * Sets up the route table. Must be called before http_router_register_route().
 */
void http_router_init(void);

/**
 * @brief Register a route with its handler
 * 
 * @param path URL path (e.g., "/", "/config", "/update")
 * @param method HTTP method (GET or POST)
 * @param handler Function to call when route matches
 * @return true if registered successfully, false if table full
 */
bool http_router_register_route(const char* path, 
                                 http_method_t method, 
                                 http_route_handler_t handler);

/**
 * @brief Find handler for a request
 * 
 * Matches the request path and method against registered routes.
 * 
 * @param request_buffer Full HTTP request string
 * @return Handler function if route found, NULL otherwise
 */
http_route_handler_t http_router_find_handler(const char* request_buffer);

#endif // HTTP_ROUTER_H
```

### Step 3: Create http_router.c Implementation

**File:** `src/network/http_router.c`

**Key components:**

1. **Route table structure:**
```c
#define HTTP_ROUTER_MAX_ROUTES 20

typedef struct {
    const char* path;
    http_method_t method;
    http_route_handler_t handler;
} http_route_entry_t;

static http_route_entry_t g_route_table[HTTP_ROUTER_MAX_ROUTES];
static size_t g_route_count = 0;
```

2. **Implementation pattern:**
- `http_router_init()` - Clear the route table
- `http_router_register_route()` - Add route to table
- `http_router_find_handler()` - Parse request, match path/method, return handler

3. **Request parsing:**
```c
// Extract method (GET/POST)
http_method_t method = HTTP_METHOD_UNKNOWN;
if (strncmp(request_buffer, "GET", 3) == 0) {
    method = HTTP_METHOD_GET;
} else if (strncmp(request_buffer, "POST", 4) == 0) {
    method = HTTP_METHOD_POST;
}

// Extract path
// Request format: "GET /config HTTP/1.1\r\n"
const char* path_start = strchr(request_buffer, ' ') + 1;
const char* path_end = strchr(path_start, ' ');
size_t path_len = path_end - path_start;
```

### Step 4: Extract Route Handlers from http_server.c

For each route in the current code, create a static handler function.

**Pattern:**
```c
// OLD CODE (inline in routing chain):
if (strstr(request_buffer, "GET /config") != NULL) {
    http_generate_config_page(response_buffer, sizeof(response_buffer));
    http_send_response(conn, response_buffer, strlen(response_buffer));
}

// NEW CODE (extracted handler):
static void http_handle_config_get(http_connection_t* conn, 
                                    const char* request_buffer, 
                                    size_t buffer_len) {
    static char response_buffer[8192];
    http_generate_config_page(response_buffer, sizeof(response_buffer));
    http_send_response(conn, response_buffer, strlen(response_buffer));
}
```

**Handlers to create (in http_server.c):**
1. `http_handle_root_get()` - Serve device page
2. `http_handle_config_get()` - Serve config page
3. `http_handle_update_get()` - Serve update page
4. `http_handle_factory_get()` - Serve factory page (conditional)
5. `http_handle_styles_get()` - Serve stylesheet
6. `http_handle_config_post()` - Process config update
7. `http_handle_password_post()` - Process password change
8. `http_handle_factory_post()` - Process factory defaults (conditional)
9. `http_handle_reboot_post()` - Process reboot request
10. `http_handle_update_post()` - Process firmware upload

### Step 5: Register Routes in http_server_init()

Add route registration after initializing the router:

```c
bool http_server_init(void) {
    // ... existing init code ...
    
    // Initialize router
    http_router_init();
    
    // Register GET routes
    http_router_register_route("/", HTTP_METHOD_GET, http_handle_root_get);
    http_router_register_route("/config", HTTP_METHOD_GET, http_handle_config_get);
    http_router_register_route("/update", HTTP_METHOD_GET, http_handle_update_get);
    http_router_register_route("/styles.css", HTTP_METHOD_GET, http_handle_styles_get);
    
    #ifdef FACTORY_INTERNAL_VERSION
    http_router_register_route("/factory", HTTP_METHOD_GET, http_handle_factory_get);
    http_router_register_route("/factory", HTTP_METHOD_POST, http_handle_factory_post);
    #endif
    
    // Register POST routes
    http_router_register_route("/change_password", HTTP_METHOD_POST, http_handle_password_post);
    http_router_register_route("/reboot", HTTP_METHOD_POST, http_handle_reboot_post);
    http_router_register_route("/update", HTTP_METHOD_POST, http_handle_update_post);
    http_router_register_route("/", HTTP_METHOD_POST, http_handle_config_post);
    
    // ... rest of init ...
}
```

### Step 6: Simplify http_connection_recv_callback()

Replace the giant if-else chain with:

```c
static err_t http_connection_recv_callback(void* arg, struct tcp_pcb* tpcb, 
                                           struct pbuf* p, err_t err) {
    // ... existing validation and auth code ...
    
    // Find and execute handler
    http_route_handler_t handler = http_router_find_handler(request_buffer);
    if (handler) {
        handler(conn, request_buffer, copy_len);
    } else {
        // 404 Not Found
        http_send_404(conn);
    }
    
    // ... cleanup code ...
}
```

### Step 7: Update CMakeLists.txt

Add http_router.c to the build:

```cmake
# Create HTTP server library
add_library(http_server
    http_server.c
    http_auth.c
    http_forms.c
    http_router.c
    http_multipart.c
)
```

### Step 8: Build and Test

```bash
cd /home/shueltenschmidt/projects/UART2ETH/build
/home/shueltenschmidt/.pico-sdk/cmake/v3.31.5/bin/cmake --build . --config Release --target uart2eth --
```

**Success criteria:**
- Build compiles without errors
- http_server.c reduced by ~200 lines
- All routes still functional (routes table matches original behavior)

### Step 9: Commit Phase 4

```bash
git add -A
git commit -m "refactor: Phase 4 - Extract HTTP Request Routing

- Created http_router.c/h with route registration system (ADR-018 Phase 4)
- Implemented http_router_init() - route table initialization
- Implemented http_router_register_route() - route registration
- Implemented http_router_find_handler() - request matching
- Extracted 10 route handlers from routing chain:
  - http_handle_root_get()
  - http_handle_config_get()
  - http_handle_update_get()
  - http_handle_factory_get() (conditional)
  - http_handle_styles_get()
  - http_handle_config_post()
  - http_handle_password_post()
  - http_handle_factory_post() (conditional)
  - http_handle_reboot_post()
  - http_handle_update_post()
- Simplified http_connection_recv_callback() to use router
- Updated CMakeLists.txt to build http_router.c
- http_server.c reduced from 826 to ~626 lines (200 lines removed)
- http_router.c: ~150 lines created
- Build verified successful

Phase 4 (Request Routing Extraction) COMPLETE per ADR-018"
```

---

## Phase 5: Cleanup and Documentation

After Phase 4, only Phase 5 remains:

### Tasks:
1. **Code cleanup:**
   - Remove any remaining dead code
   - Ensure all forward declarations are removed
   - Verify all static functions are properly scoped
   - Add any missing documentation

2. **Update arc42 documentation:**
   - Update Building Block View diagram
   - Document the new module structure
   - Add notes about http_router pattern

3. **Create test plan:**
   - List all routes and expected behavior
   - Document testing procedure for each module
   - Create manual test checklist

4. **Final verification:**
   - Complete clean build
   - Verify http_server.c is ~500 lines
   - Check that all modules are properly documented
   - Ensure ADR-018 is updated with implementation notes

5. **Create Pull Request:**
   - Write comprehensive PR description
   - List all changes and modules created
   - Highlight benefits (modularity, maintainability, testability)
   - Request review from senior developer

---

## Critical Lessons Learned from Phases 1-3

### 1. Include Path Patterns
**Pattern:** Always match existing include patterns in http_server.c
```c
// WRONG
#include "shared_memory/shared_memory.h"

// RIGHT (check http_server.c first!)
#include "shared_memory.h"
```

### 2. External Variable Access
**Issue:** Extracted modules need access to http_server.c internals
**Solutions:**
- Make variables non-static: `http_server_stats_t g_server_stats;`
- Export functions: `void http_send_response(...)` (was static)
- Add extern declarations in using modules
- Or create getter functions

### 3. Forward Declarations
**Pattern:** After extracting functions, remove their forward declarations
```c
// REMOVE after extraction:
static bool http_parse_post_data(const char* post_data, size_t data_len);

// ADD comment marker:
// Form handling functions moved to http_forms module (ADR-018 Phase 3)
```

### 4. CMakeLists.txt Integration
**Always update** `src/network/CMakeLists.txt` when adding new modules:
```cmake
add_library(http_server
    http_server.c
    http_auth.c      # Phase 2
    http_forms.c     # Phase 3
    http_router.c    # Phase 4 ← ADD THIS
    http_multipart.c
)
```

### 5. Tree-sitter Warnings Are Safe
Preprocessor directives in HTML strings cause parser warnings. **These are safe to ignore:**
```
Line 556: Syntax error: unexpected ERROR in preproc_ifdef
```
This is because HTML contains `#ifdef` in generated strings, which confuses the parser.

### 6. Build Verification Steps
After each change:
```bash
# 1. Clean build
cd /home/shueltenschmidt/projects/UART2ETH/build
/home/shueltenschmidt/.pico-sdk/cmake/v3.31.5/bin/cmake --build . --config Release --target uart2eth --

# 2. Check line counts
wc -l src/network/http_server.c src/network/http_*.c

# 3. Verify all modules present
ls -la src/network/http_*.c
ls -la include/network/http_*.h
```

---

## Common Pitfalls to Avoid

### ❌ Don't: Extract Functions Without Checking Dependencies
**Problem:** Function might depend on static variables or other static functions
**Solution:** Grep for all uses of the function first, identify dependencies

### ❌ Don't: Change Function Signatures Without Updating Callers
**Problem:** Build breaks if you change parameters
**Solution:** Search for all call sites before modifying signature

### ❌ Don't: Delete Tests
**Problem:** Violates project code of conduct
**Solution:** If tests exist (they don't yet), never delete them. Fix production code instead.

### ❌ Don't: Commit Without Testing Build
**Problem:** Broken builds frustrate the next developer
**Solution:** Always build and verify before committing

### ❌ Don't: Mix Multiple Changes in One Commit
**Problem:** Makes it hard to review and revert if needed
**Solution:** One phase = one commit (as we've been doing)

---

## Project Context Reminders

### Build Commands
```bash
# Normal build
cd /home/shueltenschmidt/projects/UART2ETH/build
/home/shueltenschmidt/.pico-sdk/cmake/v3.31.5/bin/cmake --build . --config Release --target uart2eth --

# Clean rebuild
/home/shueltenschmidt/.pico-sdk/cmake/v3.31.5/bin/cmake --build . --config Release --target uart2eth --clean-first
```

### Git Workflow
```bash
# Check current state
git branch --show-current  # Should be: feature/http-server-refactoring
git status
git log --oneline -10

# Work on files
# ... make changes ...

# Stage and commit
git add -A
git commit -m "refactor: Phase 4 - [Description]

- Detailed bullet points
- Reference ADR-018"
```

### File Structure
```
/home/shueltenschmidt/projects/UART2ETH/
├── src/
│   └── network/
│       ├── http_server.c (826 lines - target: ~500)
│       ├── http_auth.c (231 lines) ✅ Phase 2
│       ├── http_forms.c (446 lines) ✅ Phase 3
│       ├── http_router.c (??? lines) ← YOU CREATE THIS
│       ├── http_multipart.c
│       └── http_pages/ ✅ Phase 1
│           ├── CMakeLists.txt
│           └── page_*.c (5 files)
├── include/
│   └── network/
│       ├── http_server.h
│       ├── http_auth.h ✅ Phase 2
│       ├── http_forms.h ✅ Phase 3
│       ├── http_router.h ← YOU CREATE THIS
│       └── http_pages/ ✅ Phase 1
│           └── page_*.h (5 files)
└── src/docs/arc42/adrs/
    └── ADR-018-http-server-modularization.adoc
```

---

## Important Notes for Phase 4

### 1. Route Handler Pattern
Keep handlers **simple and focused**:
```c
static void http_handle_X_Y(http_connection_t* conn, 
                             const char* request_buffer, 
                             size_t buffer_len) {
    // 1. Get/allocate response buffer
    static char response_buffer[8192];
    
    // 2. Call business logic (from page/form/auth modules)
    http_generate_X_page(response_buffer, sizeof(response_buffer));
    
    // 3. Send response
    http_send_response(conn, response_buffer, strlen(response_buffer));
    
    // Optional: Close connection if needed
}
```

### 2. Conditional Compilation
The factory routes are conditional. Handle this:
```c
#ifdef FACTORY_INTERNAL_VERSION
http_router_register_route("/factory", HTTP_METHOD_GET, http_handle_factory_get);
http_router_register_route("/factory", HTTP_METHOD_POST, http_handle_factory_post);
#endif
```

### 3. Default Route (Root)
The root path "/" is used for BOTH:
- GET / → show device page
- POST / → process config update

This is valid! The router should handle same path with different methods.

### 4. Upload Handler Complexity
The `/update` POST handler is the most complex (multipart upload). You might want to keep more logic inline for this one, or extract it to a separate function. Don't over-engineer.

---

## Questions to Confirm with Senior Developer

Before starting Phase 4, check with your senior:

1. **Route table size:** Is 20 routes enough? (Currently using ~10)

2. **Path matching:** Should we support wildcards or just exact match? (Suggest: exact match for now)

3. **404 handling:** Should we create `http_send_404()` or inline it?

4. **Module location:** Should http_router.c be in `src/network/` alongside http_server.c? (Suggest: yes)

5. **Testing strategy:** Flash and test on hardware after Phase 4, or wait until Phase 5? (Suggest: wait until Phase 5)

---

## Success Criteria for Phase 4 Complete

- [ ] `include/network/http_router.h` created with API declarations
- [ ] `src/network/http_router.c` created with implementation
- [ ] Route table structure defined (max 20 routes)
- [ ] `http_router_init()` implemented
- [ ] `http_router_register_route()` implemented
- [ ] `http_router_find_handler()` implemented with path/method matching
- [ ] 10 route handlers extracted from http_server.c
- [ ] Routes registered in `http_server_init()`
- [ ] `http_connection_recv_callback()` simplified to use router
- [ ] CMakeLists.txt updated to build http_router.c
- [ ] Clean build with no errors
- [ ] Code committed with clear message referencing ADR-018 Phase 4
- [ ] http_server.c line count reduced by ~200 lines

---

## Current Branch State

```
Branch: feature/http-server-refactoring
Base: main (commit 91cb9c6)
HEAD: a6b877d
Commits ahead: 7
Status: Clean working tree, ready for Phase 4
```

**Last commit message:**
```
refactor: Phase 3 - Extract HTTP Form Handling Module

- Created http_forms.c/h with form handling functions (ADR-018 Phase 3)
- Extracted http_url_decode() - new URL decoding utility
- Extracted http_parse_form_data() - new generic form parser
- Extracted http_get_form_field() - new field accessor
- Extracted http_form_field_equals() - new field comparison
- Extracted http_validate_password_change() from http_server.c
- Extracted http_handle_password_change() from http_server.c
- Extracted http_parse_post_data() from http_server.c
- Updated CMakeLists.txt to build http_forms.c
- http_server.c reduced from 1,114 to 826 lines (288 lines removed)
- http_forms.c: 446 lines created
- Build verified successful

Phase 3 (Form Handling Extraction) COMPLETE per ADR-018
```

---

## References

- **ADR-018:** `/home/shueltenschmidt/projects/UART2ETH/src/docs/arc42/adrs/ADR-018-http-server-modularization.adoc`
- **Project Root:** `/home/shueltenschmidt/projects/UART2ETH`
- **Build Directory:** `/home/shueltenschmidt/projects/UART2ETH/build`
- **Phase 1-3 Summary:** `PHASE_1_SUMMARY.md` (if you need context)

---

## Quick Start Checklist

Ready to begin Phase 4? Follow this checklist:

1. [ ] Read this entire document
2. [ ] Review ADR-018 Section "Phase 4: Request Routing"
3. [ ] Examine current routing code in http_server.c (lines ~450-750)
4. [ ] Create `include/network/http_router.h`
5. [ ] Create `src/network/http_router.c`
6. [ ] Extract route handlers from http_server.c
7. [ ] Update `http_server_init()` to register routes
8. [ ] Simplify `http_connection_recv_callback()`
9. [ ] Update `src/network/CMakeLists.txt`
10. [ ] Build and verify
11. [ ] Commit Phase 4

Good luck with Phase 4! You're almost done - this is the second-to-last phase! 🚀

The refactoring is already a huge success:
- ✅ 65% reduction in http_server.c
- ✅ 7 well-organized modules created
- ✅ Build always clean
- ✅ Clear separation of concerns

Keep up the great work!
