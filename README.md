# HTTP Server Library (C)

A lightweight HTTP server library written in C using POSIX sockets and CMake. It listens for `GET`, `POST`, `UPDATE`, and `DELETE` requests and logs the method, client IP, and request body to the terminal.

## Requirements

- C compiler (`gcc` or `clang`)
- CMake 3.10+
- Linux / POSIX environment

## Build and Run

```bash
cmake -S . -B build
cmake --build build
./build/server_app
```

The server starts on port `8080` by default.

## Testing

### Postman

Send requests to `http://127.0.0.1:8080` using `GET`, `POST`, `UPDATE`, or `DELETE`.  
For `UPDATE`, click the method dropdown and type `UPDATE` manually. For requests with a body, use **Body -> raw**.

### curl

```bash
curl -X GET [http://127.0.0.1:8080](http://127.0.0.1:8080)
curl -X POST -d "ABC" [http://127.0.0.1:8080](http://127.0.0.1:8080)
curl -X UPDATE -d "XYZ" [http://127.0.0.1:8080](http://127.0.0.1:8080)
curl -X DELETE [http://127.0.0.1:8080](http://127.0.0.1:8080)
```

## Example Output

```text
Server listening on port 8080...
GET request from 127.0.0.1
POST request from 127.0.0.1 with body ABC
UPDATE request from 127.0.0.1 with body XYZ
DELETE request from 127.0.0.1
```
