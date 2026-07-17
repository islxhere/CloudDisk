# Response Helper Naming Design

## Goal

Replace the semantically ambiguous `response_msg` overloads with explicit success and error response helpers.

## Scope

- Rename the helper that serializes `{ status: "success", message, data }` to `respond_success`.
- Rename the helper that serializes `{ status: "error", message }` to `respond_error`.
- Update every `CloudDiskServer.cpp` call site according to the response body it currently emits.
- Preserve each route's existing HTTP status code, message text, JSON fields, and asynchronous control flow.

## Design

`respond_success(HttpResp *, const char *, const json &)` remains the only helper for successful responses that include `data`.
`respond_error(HttpResp *, const char *)` remains the only helper for error responses. Callers continue to set the appropriate HTTP status before invoking either helper.

This makes the response body's result semantics visible at every call site without broadening the change to HTTP status handling.

## Verification

- Add a focused regression check for the renamed helpers' serialized response shape if the current project test setup can exercise `HttpResp`.
- Build the CMake project and run its test suite, if configured.
- Confirm no `response_msg` references remain.
