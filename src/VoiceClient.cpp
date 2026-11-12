// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The McBopomofo Authors.

#include "VoiceClient.h"

#include <fcntl.h>
#include <fcitx/instance.h>
#include <json-c/json.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace McBopomofo {
namespace {

std::string SocketPath() {
  if (const char* env = std::getenv("VOXKIT_SOCKET")) {
    if (env[0] != '\0') return env;
  }
  if (const char* runtime = std::getenv("XDG_RUNTIME_DIR")) {
    if (runtime[0] != '\0') return std::string(runtime) + "/voxkit.sock";
  }
  return "/tmp/voxkit-" + std::to_string(static_cast<unsigned long>(::getuid())) + ".sock";
}

std::string ClipTerm(const std::string& text) {
  std::string out;
  size_t chars = 0;
  for (size_t i = 0; i < text.size() && chars < 64;) {
    auto c = static_cast<unsigned char>(text[i]);
    size_t n = 1;
    if ((c & 0x80) == 0) n = 1;
    else if ((c & 0xE0) == 0xC0) n = 2;
    else if ((c & 0xF0) == 0xE0) n = 3;
    else if ((c & 0xF8) == 0xF0) n = 4;
    else break;
    if (i + n > text.size()) break;
    out.append(text, i, n);
    i += n;
    ++chars;
  }
  return out;
}

std::string Trim(const std::string& text) {
  size_t begin = 0;
  while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
  size_t end = text.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
  return text.substr(begin, end - begin);
}

void PushTerm(std::vector<std::string>& terms, const std::string& raw) {
  if (terms.size() >= 80) return;
  std::string term = ClipTerm(Trim(raw));
  if (term.empty()) return;
  for (const auto& existing : terms) {
    if (existing == term) return;
  }
  terms.push_back(std::move(term));
}

void ReadLines(const std::string& path, bool firstColumn, std::vector<std::string>& terms) {
  std::ifstream in(path);
  if (!in) return;
  std::string line;
  while (terms.size() < 80 && std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::string trimmed = Trim(line);
    if (trimmed.empty() || trimmed[0] == '#') continue;
    if (!firstColumn) {
      PushTerm(terms, trimmed);
      continue;
    }
    std::stringstream row(trimmed);
    std::string word;
    if (row >> word) PushTerm(terms, word);
  }
}

std::string JsonString(json_object* object, const char* key) {
  json_object* value = nullptr;
  if (!object || !json_object_object_get_ex(object, key, &value) || !value) return {};
  const char* text = json_object_get_string(value);
  return text ? text : "";
}

uint64_t JsonU64(json_object* object, const char* key) {
  json_object* value = nullptr;
  if (!object || !json_object_object_get_ex(object, key, &value) || !value) return 0;
  return static_cast<uint64_t>(json_object_get_int64(value));
}

double JsonDouble(json_object* object, const char* key) {
  json_object* value = nullptr;
  if (!object || !json_object_object_get_ex(object, key, &value) || !value) return 0;
  return json_object_get_double(value);
}

}  // namespace

std::vector<std::string> LoadVoiceVocabulary(const std::string& userDataDir) {
  std::vector<std::string> terms;
  if (userDataDir.empty()) return terms;
  ReadLines(userDataDir + "/data.txt", true, terms);
  ReadLines(userDataDir + "/plus-quick-phrases.txt", false, terms);
  ReadLines(userDataDir + "/plus-english.txt", false, terms);
  return terms;
}

VoiceClient::VoiceClient(fcitx::Instance* instance) : instance_(instance) {}

VoiceClient::~VoiceClient() {
  callback_ = nullptr;
  io_.reset();
  if (fd_ >= 0 && activeReq_ != 0) {
    std::string line = std::string("{\"v\":1,\"type\":\"cancel\",\"req\":") + std::to_string(activeReq_) + "}\n";
    ssize_t n = ::send(fd_, line.data(), line.size(), MSG_NOSIGNAL);
    (void)n;
  }
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
}

void VoiceClient::setCallback(Callback callback) { callback_ = std::move(callback); }

bool VoiceClient::start(uint64_t req, const std::vector<std::string>& vocabulary, bool traditional) {
  if (fd_ < 0) {
    fd_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd_ < 0) return false;
    int flags = ::fcntl(fd_, F_GETFL, 0);
    if (flags >= 0) ::fcntl(fd_, F_SETFL, flags | O_NONBLOCK);
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::string path = SocketPath();
    if (path.size() >= sizeof(addr.sun_path)) {
      closeSocket();
      return false;
    }
    std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path.c_str());
    int rc = ::connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (rc < 0 && errno == EINPROGRESS) {
      pollfd poller{fd_, POLLOUT, 0};
      if (::poll(&poller, 1, 200) <= 0) {
        closeSocket();
        return false;
      }
      int err = 0;
      socklen_t len = sizeof(err);
      if (::getsockopt(fd_, SOL_SOCKET, SO_ERROR, &err, &len) < 0 || err != 0) {
        closeSocket();
        return false;
      }
    } else if (rc < 0) {
      closeSocket();
      return false;
    }
    armIo();
  }
  json_object* object = json_object_new_object();
  json_object_object_add(object, "v", json_object_new_int(1));
  json_object_object_add(object, "type", json_object_new_string("start"));
  json_object_object_add(object, "req", json_object_new_int64(static_cast<int64_t>(req)));
  json_object_object_add(object, "language", json_object_new_string("zh"));
  json_object_object_add(object, "convert", json_object_new_string(traditional ? "s2twp" : "none"));
  json_object* words = json_object_new_array();
  for (const auto& term : vocabulary) {
    json_object_array_add(words, json_object_new_string(term.c_str()));
  }
  json_object_object_add(object, "vocabulary", words);
  activeReq_ = req;
  sendJson(object);
  return fd_ >= 0;
}

void VoiceClient::stop(uint64_t req) {
  if (fd_ < 0) return;
  json_object* object = json_object_new_object();
  json_object_object_add(object, "v", json_object_new_int(1));
  json_object_object_add(object, "type", json_object_new_string("stop"));
  json_object_object_add(object, "req", json_object_new_int64(static_cast<int64_t>(req)));
  sendJson(object);
}

void VoiceClient::cancel(uint64_t req) {
  if (fd_ < 0) return;
  json_object* object = json_object_new_object();
  json_object_object_add(object, "v", json_object_new_int(1));
  json_object_object_add(object, "type", json_object_new_string("cancel"));
  json_object_object_add(object, "req", json_object_new_int64(static_cast<int64_t>(req)));
  sendJson(object);
  if (activeReq_ == req) activeReq_ = 0;
}

void VoiceClient::armIo() {
  if (!instance_ || fd_ < 0) return;
  auto flags = fcitx::IOEventFlags(fcitx::IOEventFlag::In);
  if (!outbuf_.empty()) flags |= fcitx::IOEventFlag::Out;
  io_ = instance_->eventLoop().addIOEvent(fd_, flags, [this](fcitx::EventSourceIO*, int, fcitx::IOEventFlags) {
    onIo();
    return fd_ >= 0;
  });
}

void VoiceClient::onIo() {
  flushOut();
  if (fd_ < 0) return;
  char buf[4096];
  while (true) {
    ssize_t n = ::recv(fd_, buf, sizeof(buf), 0);
    if (n > 0) {
      inbuf_.append(buf, static_cast<size_t>(n));
      continue;
    }
    if (n == 0) {
      closeSocket();
      if (callback_) {
        VoiceEvent event;
        event.kind = VoiceEvent::Kind::Error;
        event.req = activeReq_;
        event.code = "disconnected";
        event.message = "語音服務已斷線";
        callback_(event);
      }
      return;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) break;
    closeSocket();
    return;
  }
  size_t pos = 0;
  while (true) {
    size_t newline = inbuf_.find('\n', pos);
    if (newline == std::string::npos) break;
    handleLine(inbuf_.substr(pos, newline - pos));
    pos = newline + 1;
  }
  if (pos > 0) inbuf_.erase(0, pos);
  if (io_ && fd_ >= 0) {
    auto flags = fcitx::IOEventFlags(fcitx::IOEventFlag::In);
    if (!outbuf_.empty()) flags |= fcitx::IOEventFlag::Out;
    io_->setEvents(flags);
  }
}

void VoiceClient::flushOut() {
  while (fd_ >= 0 && !outbuf_.empty()) {
    ssize_t n = ::send(fd_, outbuf_.data(), outbuf_.size(), MSG_NOSIGNAL);
    if (n > 0) {
      outbuf_.erase(0, static_cast<size_t>(n));
      continue;
    }
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
    closeSocket();
    return;
  }
}

void VoiceClient::closeSocket() {
  io_.reset();
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
  inbuf_.clear();
  outbuf_.clear();
  activeReq_ = 0;
}

void VoiceClient::sendJson(json_object* object) {
  const char* text = json_object_to_json_string_ext(object, JSON_C_TO_STRING_PLAIN);
  if (text) {
    outbuf_.append(text);
    outbuf_.push_back('\n');
  }
  json_object_put(object);
  flushOut();
  if (io_ && fd_ >= 0) {
    auto flags = fcitx::IOEventFlags(fcitx::IOEventFlag::In);
    if (!outbuf_.empty()) flags |= fcitx::IOEventFlag::Out;
    io_->setEvents(flags);
  } else if (fd_ >= 0 && io_ == nullptr) {
    armIo();
  }
}

void VoiceClient::handleLine(const std::string& line) {
  if (!callback_ || line.empty()) return;
  json_object* object = json_tokener_parse(line.c_str());
  if (!object) return;
  VoiceEvent event;
  std::string kind = JsonString(object, "type");
  event.req = JsonU64(object, "req");
  if (kind == "partial") {
    event.kind = VoiceEvent::Kind::Partial;
    event.stable = JsonString(object, "stable");
    event.tentative = JsonString(object, "tentative");
  } else if (kind == "final") {
    event.kind = VoiceEvent::Kind::Final;
    event.text = JsonString(object, "text");
    if (activeReq_ == event.req) activeReq_ = 0;
  } else if (kind == "discarded") {
    event.kind = VoiceEvent::Kind::Discarded;
    event.message = JsonString(object, "reason");
    if (activeReq_ == event.req) activeReq_ = 0;
  } else if (kind == "error") {
    event.kind = VoiceEvent::Kind::Error;
    event.code = JsonString(object, "code");
    event.message = JsonString(object, "message");
    if (activeReq_ == event.req) activeReq_ = 0;
  } else if (kind == "level") {
    event.kind = VoiceEvent::Kind::Level;
    event.rms = JsonDouble(object, "rms_dbfs");
  } else {
    json_object_put(object);
    return;
  }
  json_object_put(object);
  callback_(event);
}

}  // namespace McBopomofo
