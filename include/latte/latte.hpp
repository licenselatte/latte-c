/*
 * C++ RAII wrapper around the latte-c public C API (latte.h).
 *
 * Header-only. Wraps latte_config / latte_sdk / latte_license in move-only
 * classes that release their underlying C resource automatically, and turns
 * latte_status failures into latte::Error exceptions instead of return codes.
 *
 *   latte::Config cfg("pk_live_...");
 *   latte::Sdk sdk(cfg);
 *   try {
 *       latte::License lic = sdk.activate(key);
 *       std::cout << lic.license_type() << "\n";
 *   } catch (const latte::Error &e) {
 *       std::cerr << e.what() << "\n";
 *   }
 */
#ifndef LATTE_HPP
#define LATTE_HPP

#include "latte/latte.h"

#include <map>
#include <stdexcept>
#include <string>
#include <utility>

namespace latte {

/* Thrown whenever a wrapped call returns a latte_status other than LATTE_OK. */
class Error : public std::runtime_error {
public:
  explicit Error(latte_status status)
      : std::runtime_error(latte_strerror(status)), status_(status) {}

  latte_status status() const noexcept { return status_; }

private:
  latte_status status_;
};

/* Owns a latte_license*; frees it on destruction. Move-only. */
class License {
public:
  License() noexcept : lic_(nullptr) {}
  explicit License(latte_license *lic) noexcept : lic_(lic) {}

  License(const License &) = delete;
  License &operator=(const License &) = delete;

  License(License &&other) noexcept : lic_(other.lic_) { other.lic_ = nullptr; }
  License &operator=(License &&other) noexcept {
    if (this != &other) {
      reset();
      lic_ = other.lic_;
      other.lic_ = nullptr;
    }
    return *this;
  }

  ~License() { reset(); }

  explicit operator bool() const noexcept { return lic_ != nullptr; }

  std::string key() const { return str(lic_->key); }
  std::string activation_id() const { return str(lic_->activation_id); }
  std::string project_id() const { return str(lic_->project_id); }
  std::string license_type() const { return str(lic_->license_type); }
  int64_t issued_at() const noexcept { return lic_->issued_at; }
  int64_t expires_at() const noexcept { return lic_->expires_at; }
  int64_t grace_period_seconds() const noexcept { return lic_->grace_period_seconds; }
  bool in_grace_period() const noexcept { return lic_->in_grace_period != 0; }

  std::map<std::string, std::string> metadata() const {
    std::map<std::string, std::string> out;
    for (uint32_t i = 0; i < lic_->metadata_count; ++i)
      out.emplace(str(lic_->metadata[i].key), str(lic_->metadata[i].value));
    return out;
  }

  /* Access the underlying C struct directly, e.g. to pass to C APIs. */
  const latte_license *raw() const noexcept { return lic_; }

private:
  static std::string str(const char *s) { return s ? std::string(s) : std::string(); }

  void reset() noexcept {
    if (lic_) {
      latte_license_free(lic_);
      lic_ = nullptr;
    }
  }

  latte_license *lic_;
};

/* Owns a latte_config*; frees it on destruction. Move-only. */
class Config {
public:
  explicit Config(const std::string &app_id) : cfg_(latte_config_new(app_id.c_str())) {
    if (!cfg_)
      throw std::invalid_argument("latte::Config: latte_config_new failed");
  }

  Config(const Config &) = delete;
  Config &operator=(const Config &) = delete;

  Config(Config &&other) noexcept : cfg_(other.cfg_) { other.cfg_ = nullptr; }
  Config &operator=(Config &&other) noexcept {
    if (this != &other) {
      reset();
      cfg_ = other.cfg_;
      other.cfg_ = nullptr;
    }
    return *this;
  }

  ~Config() { reset(); }

  /* See latte_config_set_multi_instance() in latte.h. Chainable. */
  Config &set_multi_instance(bool enabled) {
    latte_config_set_multi_instance(cfg_, enabled ? 1 : 0);
    return *this;
  }

  const latte_config *raw() const noexcept { return cfg_; }

private:
  void reset() noexcept {
    if (cfg_) {
      latte_config_free(cfg_);
      cfg_ = nullptr;
    }
  }

  latte_config *cfg_;
};

/* Owns a latte_sdk*; frees it on destruction (waiting for any in-flight
 * renewal thread, same as latte_free()). Move-only. */
class Sdk {
public:
  explicit Sdk(const Config &config) : sdk_(nullptr) {
    latte_sdk *sdk = nullptr;
    latte_status st = latte_new(config.raw(), &sdk);
    if (st != LATTE_OK)
      throw Error(st);
    sdk_ = sdk;
  }

  Sdk(const Sdk &) = delete;
  Sdk &operator=(const Sdk &) = delete;

  Sdk(Sdk &&other) noexcept : sdk_(other.sdk_) { other.sdk_ = nullptr; }
  Sdk &operator=(Sdk &&other) noexcept {
    if (this != &other) {
      reset();
      sdk_ = other.sdk_;
      other.sdk_ = nullptr;
    }
    return *this;
  }

  ~Sdk() { reset(); }

  /* Throws latte::Error on any status other than LATTE_OK. */
  License activate(const std::string &key) {
    latte_license *lic = nullptr;
    latte_status st = latte_activate(sdk_, key.c_str(), &lic);
    if (st != LATTE_OK)
      throw Error(st);
    return License(lic);
  }

  /* Throws latte::Error on any status other than LATTE_OK. */
  License check() {
    latte_license *lic = nullptr;
    latte_status st = latte_check(sdk_, &lic);
    if (st != LATTE_OK)
      throw Error(st);
    return License(lic);
  }

private:
  void reset() noexcept {
    if (sdk_) {
      latte_free(sdk_);
      sdk_ = nullptr;
    }
  }

  latte_sdk *sdk_;
};

} // namespace latte

#endif /* LATTE_HPP */
