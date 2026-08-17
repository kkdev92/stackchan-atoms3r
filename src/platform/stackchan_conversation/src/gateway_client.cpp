#include "stackchan/conversation/gateway_client.hpp"

#include <array>
#include <cstdio>
#include <cstring>

#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"

#include "stackchan/domain/json_writer.hpp"
#include "stackchan/domain/wav.hpp"
#include "stackchan/identity/device.hpp"
#include "stackchan/identity/token.hpp"

namespace stackchan::conversation {
namespace {

constexpr char kTag[] = "gateway";

constexpr char kNvsNamespace[] = "gateway";
constexpr char kKeyUrl[] = "url";

// The deadlines from the contract.
constexpr std::uint32_t kFirstEventDeadlineMs = 10000;
constexpr std::uint32_t kIdleDeadlineMs = 30000;

// How long one read waits. Cancellation takes effect at this granularity.
constexpr int kReadTimeoutMs = 1000;

// Where a text request body is assembled: the utterance limit, plus room
// for escaping to expand it, plus the envelope.
constexpr std::size_t kMaxTextBodyBytes = 3072;

std::array<char, kMaxUrlLength + 1> g_url{};

[[nodiscard]] std::uint32_t now_ms() {
  return static_cast<std::uint32_t>(esp_timer_get_time() / 1000);
}

// Copy a string_view into the null-terminated form the HTTP client wants.
struct CString {
  std::array<char, 128> text{};
  explicit CString(std::string_view source) {
    const std::size_t length =
        source.size() < text.size() - 1 ? source.size() : text.size() - 1;
    std::memcpy(text.data(), source.data(), length);
    text[length] = 0;
  }
};

}  // namespace

void load_gateway_url() {
  nvs_handle_t handle = 0;
  if (nvs_open(kNvsNamespace, NVS_READONLY, &handle) != ESP_OK) {
    return;  // no namespace at all means unconfigured, which is not an error
  }
  std::size_t length = g_url.size();
  if (nvs_get_str(handle, kKeyUrl, g_url.data(), &length) != ESP_OK) {
    g_url[0] = 0;
  }
  nvs_close(handle);
  if (g_url[0] != 0) {
    ESP_LOGI(kTag, "gateway url loaded: %s", g_url.data());
  }
}

bool save_gateway_url(std::string_view url) {
  // http:// only; there is no TLS here yet. Storing something malformed
  // would turn into an unexplained failure at every conversation instead.
  if (url.size() < 8 || url.size() > kMaxUrlLength ||
      url.substr(0, 7) != "http://") {
    return false;
  }
  for (const char c : url) {
    if (c <= ' ' || c == '"') {
      return false;
    }
  }

  std::array<char, kMaxUrlLength + 1> text{};
  std::memcpy(text.data(), url.data(), url.size());

  nvs_handle_t handle = 0;
  if (nvs_open(kNvsNamespace, NVS_READWRITE, &handle) != ESP_OK) {
    return false;
  }
  const bool wrote = nvs_set_str(handle, kKeyUrl, text.data()) == ESP_OK &&
                     nvs_commit(handle) == ESP_OK;
  nvs_close(handle);

  if (wrote) {
    g_url = text;
    ESP_LOGI(kTag, "gateway url stored: %s", g_url.data());
  }
  return wrote;
}

std::string_view gateway_url() { return std::string_view{g_url.data()}; }

bool converse(const ConverseRequest& request, app::ConversationTurn& turn,
              runtime::CancellationToken token) {
  // With words to say, no audio is needed. With neither, there is nothing
  // to send.
  const bool sending_text = !request.text.empty();
  if (g_url[0] == 0 || (!sending_text && request.pcm == nullptr)) {
    turn.abort(domain::ErrorCode::unavailable);
    return false;
  }

  // Build {gateway_url}/v1/converse.
  std::array<char, kMaxUrlLength + 16> url{};
  std::snprintf(url.data(), url.size(), "%s/v1/converse", g_url.data());

  esp_http_client_config_t config = {};
  config.url = url.data();
  config.method = HTTP_METHOD_POST;
  // Short, so that reads return regularly and cancellation and the
  // deadlines can be checked in between; a timeout surfaces as EAGAIN
  // rather than as an error.
  config.timeout_ms = kReadTimeoutMs;
  config.buffer_size = 2048;
  config.buffer_size_tx = 1024;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) {
    turn.abort(domain::ErrorCode::internal);
    return false;
  }

  // Authentication and identity. Unlike interpreting the response, this is
  // platform knowledge.
  const CString token_text{identity::access_token().text()};
  const CString device_text{identity::collect().device_id};
  const CString boot_text{identity::collect().boot_id.text()};
  const CString conversation_text{request.conversation_id};
  esp_http_client_set_header(client, "X-StackChan-Token", token_text.text.data());
  esp_http_client_set_header(client, "X-StackChan-Device", device_text.text.data());
  esp_http_client_set_header(client, "X-StackChan-Boot", boot_text.text.data());
  esp_http_client_set_header(client, "X-StackChan-Conversation",
                             conversation_text.text.data());
  esp_http_client_set_header(client, "Content-Type",
                             sending_text ? "application/json" : "audio/wav");
  esp_http_client_set_header(client, "Accept", "text/event-stream");

  bool transport_ok = false;

  do {
    // Build the body: JSON for words, a WAV for audio. Both have a known
    // length, so both can be sent with Content-Length.
    //
    // Escaping is JsonWriter's job, so quotes or control characters in the
    // text cannot produce a malformed request.
    static std::array<char, kMaxTextBodyBytes> body{};
    int total = 0;

    if (sending_text) {
      domain::JsonWriter writer{body.data(), body.size()};
      writer.begin_object();
      writer.member("text", request.text);
      writer.end_object();
      if (!writer.valid()) {
        ESP_LOGW(kTag, "the text did not fit in %u bytes",
                 static_cast<unsigned>(body.size()));
        turn.abort(domain::ErrorCode::invalid_argument);
        break;
      }
      total = static_cast<int>(writer.length());
    }
    else {
      total = static_cast<int>(domain::kWavHeaderBytes + request.pcm_bytes);
    }

    if (esp_http_client_open(client, total) != ESP_OK) {
      ESP_LOGW(kTag, "could not reach the gateway at %s", url.data());
      turn.abort(domain::ErrorCode::unavailable);
      break;
    }

    bool write_failed = false;

    if (sending_text) {
      write_failed = esp_http_client_write(client, body.data(), total) != total;
    }
    else {
      std::array<std::uint8_t, domain::kWavHeaderBytes> header{};
      domain::write_wav_header(header, static_cast<std::uint32_t>(request.pcm_bytes));

      if (esp_http_client_write(client,
                                reinterpret_cast<const char*>(header.data()),
                                static_cast<int>(header.size())) !=
          static_cast<int>(header.size())) {
        turn.abort(domain::ErrorCode::unavailable);
        break;
      }

      // The PCM goes out in chunks, since one write may not take all of it.
      std::size_t sent = 0;
      while (sent < request.pcm_bytes) {
        if (token.cancelled()) {
          break;
        }
        const int wrote = esp_http_client_write(
            client, reinterpret_cast<const char*>(request.pcm) + sent,
            static_cast<int>(request.pcm_bytes - sent));
        if (wrote <= 0) {
          write_failed = true;
          break;
        }
        sent += static_cast<std::size_t>(wrote);
      }
    }

    if (token.cancelled()) {
      turn.abort(domain::ErrorCode::cancelled);
      break;
    }
    if (write_failed) {
      turn.abort(domain::ErrorCode::unavailable);
      break;
    }

    // Wait for the response headers, within the first-event deadline.
    // Each timeout returns EAGAIN, which is the opportunity to notice a
    // cancellation.
    const std::uint32_t started = now_ms();
    std::int64_t fetched = -ESP_ERR_HTTP_EAGAIN;
    while (true) {
      fetched = esp_http_client_fetch_headers(client);
      if (fetched != -ESP_ERR_HTTP_EAGAIN) {
        break;
      }
      if (token.cancelled()) {
        turn.abort(domain::ErrorCode::cancelled);
        break;
      }
      if (now_ms() - started >= kFirstEventDeadlineMs) {
        ESP_LOGW(kTag, "no response headers within %ums",
                 static_cast<unsigned>(kFirstEventDeadlineMs));
        turn.abort(domain::ErrorCode::timeout);
        break;
      }
    }
    if (turn.outcome() != app::ConversationTurn::Outcome::running) {
      break;
    }
    if (fetched < 0) {
      turn.abort(domain::ErrorCode::unavailable);
      break;
    }

    const int status = esp_http_client_get_status_code(client);
    if (status != 200) {
      ESP_LOGW(kTag, "gateway answered %d", status);
      // 401 and 403 mean the configuration is wrong, 5xx means the other
      // end is unwell. Both are unreachable as far as this turn goes.
      turn.abort(domain::ErrorCode::unavailable);
      break;
    }

    // The body is an event stream. Blocks go straight to core, split
    // however they arrive.
    std::array<char, 1024> chunk{};
    std::uint32_t last_data = now_ms();
    bool closed = false;
    while (turn.outcome() == app::ConversationTurn::Outcome::running) {
      if (token.cancelled()) {
        turn.abort(domain::ErrorCode::cancelled);
        break;
      }
      const int got =
          esp_http_client_read(client, chunk.data(), static_cast<int>(chunk.size()));
      if (got > 0) {
        last_data = now_ms();
        turn.feed(std::string_view{chunk.data(), static_cast<std::size_t>(got)});
        continue;
      }
      if (got == -ESP_ERR_HTTP_EAGAIN) {
        // Nothing available yet. Keep waiting, watching only the
        // between-events deadline.
        if (now_ms() - last_data >= kIdleDeadlineMs) {
          ESP_LOGW(kTag, "stream went quiet for %ums",
                   static_cast<unsigned>(kIdleDeadlineMs));
          turn.abort(domain::ErrorCode::timeout);
          break;
        }
        continue;
      }
      if (got == 0) {
        // The other end closed. If a completion event arrived, that is a
        // normal finish.
        closed = true;
        turn.finish_input();
        break;
      }
      ESP_LOGW(kTag, "stream read failed (%d)", got);
      turn.abort(domain::ErrorCode::unavailable);
      break;
    }
    // When the completion event arrives first, the loop exits without
    // waiting for the close. That still counts as a complete exchange.
    transport_ok =
        closed || turn.outcome() == app::ConversationTurn::Outcome::completed;
  } while (false);

  // Closing from this side is how a cancellation reaches the gateway.
  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  return transport_ok;
}

}  // namespace stackchan::conversation
