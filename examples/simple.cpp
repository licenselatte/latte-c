/*
 * C++ port of examples/simple.c using the latte::Config / latte::Sdk /
 * latte::License RAII wrapper from <latte/latte.hpp>.
 */
#include <cstring>
#include <iostream>

#include "latte/latte.hpp"

/* Replace with a real pk_live_... key from the dashboard. */
static const char *SDK_TEST_APP_KEY = "pk_live_G98S8BKDDMZW32RMQM8NZT33E5CTFEDC";

/* The following key is always valid and has unlimited activations:
 * G98S8-BWAR6-Y0Z7F-JT10A-7EWD9-33265 */

int main() {
  try {
    latte::Config cfg(SDK_TEST_APP_KEY);
    latte::Sdk sdk(cfg);

    std::cout << "Type your license key (e.g. XXXXX-XXXXX-XXXXX-XXXXX-XXXXX-XXXXX):\n> ";
    std::string input;
    if (!std::getline(std::cin, input))
      return 1;

    try {
      latte::License lic = sdk.activate(input);
      std::cout << "License activated!\n";
      std::cout << "  Key:          " << lic.key() << "\n";
      std::cout << "  ActivationID: " << lic.activation_id() << "\n";
      std::cout << "  LicenseType:  " << lic.license_type() << "\n";
      std::cout << "  IssuedAt:     " << lic.issued_at() << "\n";
      std::cout << "  ExpiresAt:    " << lic.expires_at() << "\n";
      std::cout << "  InGracePeriod:" << lic.in_grace_period() << "\n";
    } catch (const latte::Error &e) {
      std::cerr << "Activation error: " << e.what() << "\n";
      return 1;
    }
  } catch (const latte::Error &e) {
    std::cerr << "Failed to init SDK: " << e.what() << "\n";
    return 1;
  }

  return 0;
}
