# Native Worker Abort Failure
When terminating a worker thread that has a busy uv thread pool, Node will core dump.

## To see failure
- Install the package with `npm install`
- Run `npm test`
