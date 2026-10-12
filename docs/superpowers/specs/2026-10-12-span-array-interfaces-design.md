# std::span Array Interface Design

## Goal

Reduce avoidable copies and conversions between `QVector` and `std::vector` by using `std::span` for synchronous, non-owning array parameters throughout the project, while preserving the correct ownership and Qt integration boundaries.

## Design

- Use `std::span<const T>` for functions that only read contiguous caller-owned arrays, including detection-box/class-name rendering and preview APIs, plus batch-source construction where the callee only consumes the supplied range during the call.
- Use `std::span<T>` for in-place operations such as mapping boxes into original-image coordinates. Keep the current caller-owned container and mutate its elements without constructing a second result container.
- Keep storage-owning values as owning containers. In particular, retain Qt containers for QML properties, queued Qt signals/slots, and Qt-facing result/configuration values; retain `std::vector` where ownership or an external API requires it, including ONNX Runtime values and task ownership lists.
- Use `std::vector` for detector-local arrays passed to OpenCV NMS. Pass those vectors directly to OpenCV and eliminate the current `QVector`/`std::vector` adapter copies. Detector parsing can use `std::vector` internally and convert only at the Qt result boundary.
- Update all declarations, definitions, and callers together. Do not introduce spans that outlive the called function or cross asynchronous/thread boundaries.

## Alternatives

1. **Recommended: borrowed spans at synchronous array interfaces, owning containers at storage/Qt boundaries.** This removes unnecessary conversions while keeping lifetime and Qt semantics explicit.
2. Migrate most internal owning Qt containers to `std::vector` and convert at Qt boundaries. This broadens the migration and can add conversions where Qt integration requires owned Qt values.
3. Change only the NMS code. This is smaller but leaves other synchronous array interfaces tied to `QVector` and does not satisfy the project-wide interface goal.

## Compatibility and error handling

No intended behavior changes. Spans are valid only for the duration of synchronous calls; existing containers remain the owners. Existing Qt signal payloads and QML-visible types remain unchanged. Invalid/empty input behavior should remain as currently defined by each caller.

## Validation

Build the existing CMake project and search the source tree for remaining conversions between `QVector` and `std::vector`. Confirm that remaining Qt containers are owning/Qt-boundary values and that all changed function signatures have updated call sites.
