/* Copyright 2024 Automated Algo (www.automatedalgo.com)

This file is part of Automated Algo's "Apex" project.

Apex is free software: you can redistribute it and/or modify it under the terms
of the GNU Lesser General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.

Apex is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License along
with Apex. If not, see <https://www.gnu.org/licenses/>.
*/

#include <apex/util/Ed25519Signer.hpp>
#include "quicktest.hpp"

using namespace std;
using namespace apex;

TEST_CASE("sign_detach")
{
  // seed: 6042b842f037185244cca0cb430b450551ea33c783155192f0af2998fae1664f

  const auto private_key = "6042b842f037185244cca0cb430b450551ea33c783155192f0af2998fae1664f5dec4f836c2ff0555fa2a0eebd533ae912d0ba6ab4744687c014fcf9455b2d8c";

  const Ed25519Signer signer(private_key);

  const auto signature = signer.sign_detached("hello world");

  REQUIRE(signature.to_hex() == "5ecac205843ab33b1815911a96af07a491db83013ef9a32c9b8139689dfa51120dc5d28a60d0053fbe7137192ad8052296b82f409e28f7f4a49ecf2c5e98e70c");

  REQUIRE(signature.to_base64() == "XsrCBYQ6szsYFZEalq8HpJHbgwE++aMsm4E5aJ36URINxdKKYNAFP75xNxkq2AUilrgvQJ4o9/Skns8sXpjnDA==");
}


int main(int argc, char** argv)
{
  try {
    int result = quicktest::run(argc, argv);
    return (result < 0xFF ? result : 0xFF);
  } catch (exception& e) {
    cout << e.what() << endl;
    return 1;
  }
}
