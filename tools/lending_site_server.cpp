/*
Purpose: Minimal static file server for the FunPay Vertex website.
*/
#include <array>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {
#ifdef _WIN32
using SocketType = SOCKET;
const SocketType kInvalidSocket = INVALID_SOCKET;
#else
using SocketType = int;
const SocketType kInvalidSocket = -1;
#endif

void CloseSocket(SocketType socket_handle) {
#ifdef _WIN32
  closesocket(socket_handle);
#else
  close(socket_handle);
#endif
}

bool SendAll(SocketType socket_handle, const std::string &data) {
  std::size_t total = 0;
  while (total < data.size()) {
#ifdef _WIN32
    int sent = send(socket_handle, data.data() + total, static_cast<int>(data.size() - total), 0);
#else
    ssize_t sent = send(socket_handle, data.data() + total, data.size() - total, 0);
#endif
    if (sent <= 0) {
      return false;
    }
    total += static_cast<std::size_t>(sent);
  }
  return true;
}

std::string GetMimeType(const std::filesystem::path &path) {
  const auto ext = path.extension().string();
  if (ext == ".html") {
    return "text/html; charset=utf-8";
  }
  if (ext == ".css") {
    return "text/css; charset=utf-8";
  }
  if (ext == ".js") {
    return "application/javascript; charset=utf-8";
  }
  if (ext == ".woff2") {
    return "font/woff2";
  }
  if (ext == ".svg") {
    return "image/svg+xml";
  }
  return "application/octet-stream";
}

bool IsSafePath(std::string_view path) {
  return path.find("..") == std::string_view::npos && path.find('\\') == std::string_view::npos;
}

bool ReadFile(const std::filesystem::path &path, std::string *out) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return false;
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  *out = buffer.str();
  return true;
}

void SendResponse(SocketType socket_handle, int status_code, std::string_view status_text,
                  std::string_view content_type, const std::string &body) {
  std::ostringstream response;
  response << "HTTP/1.1 " << status_code << " " << status_text << "\r\n";
  response << "Content-Type: " << content_type << "\r\n";
  response << "Content-Length: " << body.size() << "\r\n";
  response << "Connection: close\r\n\r\n";
  response << body;
  SendAll(socket_handle, response.str());
}

void HandleClient(SocketType socket_handle, const std::filesystem::path &root) {
  std::array<char, 4096> buffer{};
#ifdef _WIN32
  int received = recv(socket_handle, buffer.data(), static_cast<int>(buffer.size() - 1), 0);
#else
  ssize_t received = recv(socket_handle, buffer.data(), buffer.size() - 1, 0);
#endif
  if (received <= 0) {
    return;
  }

  buffer[static_cast<std::size_t>(received)] = '\0';
  std::string request(buffer.data());
  const auto line_end = request.find("\r\n");
  if (line_end == std::string::npos) {
    return;
  }

  std::istringstream line_stream(request.substr(0, line_end));
  std::string method;
  std::string target;
  std::string version;
  line_stream >> method >> target >> version;

  if (method != "GET") {
    const std::string body = "Method Not Allowed";
    SendResponse(socket_handle, 405, "Method Not Allowed", "text/plain; charset=utf-8", body);
    return;
  }

  const auto query_pos = target.find('?');
  if (query_pos != std::string::npos) {
    target = target.substr(0, query_pos);
  }

  if (target.empty() || target[0] != '/' || !IsSafePath(target)) {
    const std::string body = "Bad Request";
    SendResponse(socket_handle, 400, "Bad Request", "text/plain; charset=utf-8", body);
    return;
  }

  if (target == "/") {
    target = "/index.html";
  }

  const auto relative_path = target.substr(1);
  const auto file_path = root / relative_path;

  std::string body;
  if (!ReadFile(file_path, &body)) {
    const std::string not_found = "Not Found";
    SendResponse(socket_handle, 404, "Not Found", "text/plain; charset=utf-8", not_found);
    return;
  }

  SendResponse(socket_handle, 200, "OK", GetMimeType(file_path), body);
}

int ParsePort(const char *value) {
  if (!value) {
    return 8080;
  }
  try {
    const int port = std::stoi(value);
    if (port > 0 && port <= 65535) {
      return port;
    }
  } catch (const std::exception &) {
  }
  return 8080;
}
}

int main(int argc, char **argv) {
  const std::filesystem::path root = argc > 1
    ? std::filesystem::path(argv[1])
    : std::filesystem::current_path() / "web";
  const int port = ParsePort(argc > 2 ? argv[2] : nullptr);

  if (!std::filesystem::exists(root)) {
    std::cerr << "Root directory not found: " << root << "\n";
    return 1;
  }

#ifdef _WIN32
  WSADATA wsa_data;
  if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
    std::cerr << "WSAStartup failed\n";
    return 1;
  }
#endif

  SocketType server_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (server_socket == kInvalidSocket) {
    std::cerr << "Socket creation failed\n";
    return 1;
  }

  int reuse = 1;
#ifdef _WIN32
  setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&reuse), sizeof(reuse));
#else
  setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(static_cast<uint16_t>(port));

  if (bind(server_socket, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0) {
    std::cerr << "Bind failed\n";
    CloseSocket(server_socket);
    return 1;
  }

  if (listen(server_socket, 10) < 0) {
    std::cerr << "Listen failed\n";
    CloseSocket(server_socket);
    return 1;
  }

  std::cout << "Serving " << root << " on http://localhost:" << port << "\n";

  while (true) {
    sockaddr_in client_address{};
#ifdef _WIN32
    int client_len = sizeof(client_address);
    SocketType client_socket = accept(server_socket, reinterpret_cast<sockaddr *>(&client_address), &client_len);
#else
    socklen_t client_len = sizeof(client_address);
    SocketType client_socket = accept(server_socket, reinterpret_cast<sockaddr *>(&client_address), &client_len);
#endif
    if (client_socket == kInvalidSocket) {
      continue;
    }

    HandleClient(client_socket, root);
    CloseSocket(client_socket);
  }

  CloseSocket(server_socket);
#ifdef _WIN32
  WSACleanup();
#endif
  return 0;
}
