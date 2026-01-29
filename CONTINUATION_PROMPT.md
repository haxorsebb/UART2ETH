# HTTP Server Refactoring - Continuation Prompt for Next Developer

## Current Status (as of commit f8f48c6)

You are continuing the HTTP server refactoring work on the UART2ETH project. **Phase 1 is NOW FULLY COMPLETE** and committed on branch `feature/http-server-refactoring`.

### What's Been Completed - Phase 1: Page Extraction ✅

**Commits on feature/http-server-refactoring branch:**
1. `16172ee` - ADR-018 documentation created
2. `3b36d73` - Page modules extracted (page_*.c and page_*.h files created)
3. `27052aa` - Factory defaults page added
4. `743d8a9` - Header files moved to include directory
5. `f8f48c6` - **Phase 1 integration completed** (THIS IS WHERE WE ARE NOW)

**Files Created:**
- `src/network/http_pages/CMakeLists.txt` - Build configuration for page modules
- `src/network/http_pages/page_device.c` (230 lines)
- `src/network/http_pages/page_config.c` (250 lines)
- `src/network/http_pages/page_update.c` (130 lines)
- `src/network/http_pages/page_styles.c` (100 lines)
- `src/network/http_pages/page_factory.c` (380 lines, conditional with #ifdef FACTORY_INTERNAL_VERSION)
- `include/network/http_pages/*.h` (5 header files)

**Code Changes:**
- ✅ All page generation code extracted from http_server.c
- ✅ Pages now use external stylesheet via `/styles.css` link
- ✅ CMakeLists.txt integration complete (http_pages subdirectory added)
- ✅ Build system working (confirmed successful build)
- ✅ **http_server.c reduced from 2,349 to 1,283 lines** (1,066 lines removed!)

**Important Fixes Applied:**
- Made `g_server_stats` non-static in http_server.c so page modules can access it
- Added `#include "network/http_server.h"` to page_device.c for type definitions
- Implemented missing `http_handle_reboot_request()` function

**Build Status:** ✅ Clean build, all modules compiling successfully

---

## What Remains - Phases 2-5 (Per ADR-018)

### Phase 2: Extract Authentication (~200 lines) - NEXT UP

**Goal:** Move authentication logic to dedicated module

**Files to create:**
- `src/network/http_auth.c`
- `include/network/http_auth.h`

**Functions to extract from http_server.c:**
1. `http_base64_decode()` - Currently at line ~865-920
2. `http_check_authentication()` - Currently at line ~960-1010
3. `http_send_auth_required()` - Currently at line ~920-950

**API to implement (from ADR-018):**
```c
int http_base64_decode(const char* input, char* output, size_t max_len);
int http_base64_encode(const uint8_t* input, size_t input_len, char* output, size_t max_len);
bool http_check_authentication(const char* request, const char* expected_password);
void http_send_auth_required(http_connection_t* conn);
```

**Dependencies:**
- Will need `shared_memory.h` for password access
- Will need `http_server.h` for http_connection_t type
- Functions are currently static in http_server.c

**Steps:**
1. Create include/network/http_auth.h with API declarations
2. Create src/network/http_auth.c with implementations
3. Extract the 3 functions from http_server.c
4. Remove static keyword, update function signatures if needed
5. Add http_auth includes to http_server.c
6. Update CMakeLists.txt to build http_auth.c
7. Build and verify authentication still works
8. Commit with message referencing ADR-018 Phase 2

---

### Phase 3: Extract Form Handling (~300 lines)

**Files to create:**
- `src/network/http_forms.c`
- `include/network/http_forms.h`

**Functions to extract:**
- `http_parse_post_data()` - Main form parser
- `http_parse_form_data()` - Generic form field extractor
- `http_get_form_field()` - Field accessor by name
- `http_url_decode()` - URL decoding helper
- `http_handle_password_change()` - Password change logic
- Form validation helpers

**Used by:**
- Configuration page POST handler
- Password change POST handler
- Factory defaults POST handler

---

### Phase 4: Extract Request Routing (~200 lines)

**Files to create:**
- `src/network/http_router.c`
- `include/network/http_router.h`

**Create route registration system:**
```c
typedef void (*http_route_handler_t)(http_connection_t*, const char*, size_t);

void http_router_init(void);
bool http_router_register_route(const char* path, http_request_type_t method, http_route_handler_t handler);
http_route_handler_t http_router_find_route(const char* path, http_request_type_t method);
```

**Routes to register:**
- GET `/` → device page
- GET `/config` → config page
- GET `/update` → update page
- GET `/factory` → factory page (conditional)
- GET `/styles.css` → stylesheet
- POST `/` → config save
- POST `/change_password` → password change
- POST `/factory` → factory defaults write
- POST `/update` → firmware upload
- POST `/reboot` → device reboot

**Current routing:** Manual string matching in http_connection_recv_callback()

---

### Phase 5: Cleanup and Documentation

1. Remove all extracted code from http_server.c (should reduce to ~500 lines)
2. Update includes in http_server.c
3. Update main CMakeLists.txt
4. Run complete build test
5. Update arc42 Building Block View diagram
6. Create comprehensive test plan
7. Update ADR-018 with implementation notes
8. Create Pull Request

---

## Critical Lessons Learned from Phase 1

### 1. External Variable Access
**Issue:** Page modules needed access to `g_server_stats` from http_server.c  
**Solution:** Made the variable non-static and added extern declaration in page modules  
**Pattern:** If new modules need access to http_server.c internals, either:
- Make the variable/function non-static, OR
- Create a getter function in http_server.h

### 2. Type Dependencies
**Issue:** page_device.c needed `http_server_stats_t` type definition  
**Solution:** Added `#include "network/http_server.h"` to the .c file  
**Pattern:** Extracted modules will need to include http_server.h for shared types

### 3. Missing Function Implementations
**Issue:** `http_handle_reboot_request()` was declared but never implemented  
**Solution:** Implemented the function using `watchdog_reboot(0, 0, 1)`  
**Pattern:** Check for all forward declarations and ensure implementations exist

### 4. Tree-sitter Warnings Are Safe
Preprocessor directives inside string literals (like `#ifdef` in HTML generation) cause tree-sitter parser warnings. These are **safe to ignore** - they're a parser limitation, not actual syntax errors.

### 5. Build System Integration
The http_pages subdirectory was already added to CMakeLists.txt in earlier commits. Always verify:
```bash
# Check if subdirectory is added
grep -r "add_subdirectory(http_pages)" src/network/CMakeLists.txt

# Verify module is linked
grep -r "http_pages" src/network/CMakeLists.txt
```

---

## Project Context Reminders

### Build Commands
```bash
# Clean build
cd /home/shueltenschmidt/projects/UART2ETH/build
/home/shueltenschmidt/.pico-sdk/cmake/v3.31.5/bin/cmake --build . --config Release --target uart2eth --clean-first

# Normal build
/home/shueltenschmidt/.pico-sdk/cmake/v3.31.5/bin/cmake --build . --config Release --target uart2eth --
```

### Git Workflow
```bash
# Check current branch and status
git branch --show-current
git status

# Stage and commit
git add -A
git commit -m "refactor: [Phase X] - [Description]

- Bullet point changes
- Reference ADR-018"
```

### File Structure
```
/home/shueltenschmidt/projects/UART2ETH/
├── src/
│   └── network/
│       ├── http_server.c (1,283 lines - was 2,349)
│       ├── http_multipart.c
│       └── http_pages/           ← Phase 1 COMPLETE
│           ├── CMakeLists.txt    ← Phase 1 COMPLETE
│           ├── page_*.c (5 files)
├── include/
│   └── network/
│       ├── http_server.h
│       └── http_pages/           ← Phase 1 COMPLETE
│           └── page_*.h (5 files)
└── src/docs/arc42/adrs/
    └── ADR-018-http-server-modularization.adoc
```

---

## Recommended Next Steps for Phase 2

### Step 1: Read ADR-018 Phase 2 section
```bash
view /home/shueltenschmidt/projects/UART2ETH/src/docs/arc42/adrs/ADR-018-http-server-modularization.adoc
```

### Step 2: Identify authentication functions in http_server.c
```bash
# Find Base64 decode
grep -n "http_base64_decode" src/network/http_server.c

# Find authentication check
grep -n "http_check_authentication" src/network/http_server.c

# Find auth required response
grep -n "http_send_auth_required" src/network/http_server.c
```

### Step 3: Create http_auth.h header
Follow the API specification in ADR-018 section "HTTP Authentication API"

### Step 4: Create http_auth.c implementation
Extract the functions, ensure they compile, update http_server.c to use new module

### Step 5: Update CMakeLists.txt
Add http_auth.c to the build (likely in `src/network/CMakeLists.txt`, similar to http_pages)

### Step 6: Test and commit
Build, verify authentication works, commit with clear message

---

## Important Coding Discipline Reminders

### From Project Code of Conduct:
- ✅ Create feature branch - DONE (on feature/http-server-refactoring)
- ✅ Update arc42 before writing code - DONE (ADR-018 exists)
- ⚠️ **Do not delete tests** - No tests modified in Phase 1, continue this pattern
- ⚠️ **Commit incrementally** - Each phase should be one logical commit
- ⚠️ **Write for readability** - Not brevity, not performance

### Testing After Each Phase:
While there are no automated tests yet, manual verification is recommended:
1. Build succeeds
2. Code compiles without warnings (tree-sitter warnings OK)
3. Function signatures match ADR-018 specifications
4. No regressions in existing functionality

---

## Questions to Confirm with Senior Developer Before Phase 2

1. **Should Phase 2 start immediately?** Or review Phase 1 first?

2. **CMakeLists.txt location:** Should http_auth.c be added to `src/network/CMakeLists.txt` alongside http_server.c? (Most likely yes)

3. **Testing approach:** Should we flash and test the device after Phase 2 to verify authentication still works? Or defer until all phases complete?

4. **Function visibility:** The extracted auth functions are currently static. Should they remain in the http_auth namespace (http_auth_*) or keep original names (http_*)?

---

## Success Criteria for Phase 2 Complete

- [ ] `include/network/http_auth.h` created with API declarations
- [ ] `src/network/http_auth.c` created with implementations
- [ ] Base64 decode/encode functions extracted and working
- [ ] Authentication check function extracted and working
- [ ] Auth required response function extracted and working
- [ ] `http_server.c` updated to use new http_auth module
- [ ] CMakeLists.txt updated to build http_auth
- [ ] Clean build with no errors
- [ ] Code committed with clear message referencing ADR-018 Phase 2
- [ ] http_server.c line count reduced by ~200 lines

---

## Current Branch State

```
Branch: feature/http-server-refactoring
Base: main (commit 91cb9c6)
HEAD: f8f48c6
Files changed: 14 files
Lines added: 1,337
Lines deleted: 1,066
Status: Clean working tree, ready for Phase 2
```

**Last commit message:**
```
refactor: Complete Phase 1 integration - HTTP page modules

- Created http_pages/CMakeLists.txt for page module build config
- Made g_server_stats non-static for page module access
- Added #include network/http_server.h to page_device.c for stats type
- Implemented missing http_handle_reboot_request() function
- All page modules now fully integrated and building successfully

Phase 1 (Page Extraction) now COMPLETE per ADR-018
```

---

## References

- **ADR-018:** `/home/shueltenschmidt/projects/UART2ETH/src/docs/arc42/adrs/ADR-018-http-server-modularization.adoc`
- **Project Root:** `/home/shueltenschmidt/projects/UART2ETH`
- **Original Prompt:** See project documentation in continuation prompt document
- **Build Directory:** `/home/shueltenschmidt/projects/UART2ETH/build`

Good luck with Phase 2! 🚀
