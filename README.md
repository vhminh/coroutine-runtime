# coroutine runtime

Stackful coroutine runtime for x86

## Development
- Generate compile_commands.json:
  ```sh
  make clean && rm -rf compile_commands.json && bear -- make all -j
  ```
- Run examples:
  ```sh
  make -j && ./out/examples/probe-uring-version
  ```

