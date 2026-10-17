#pragma once

// Single DES in CBC mode, enough for the login server's credential block.
// The 2002 client encrypts {user[20], pass[20]} with Verant's fixed key, which
// is also used as the IV (server/loginserver/eq_crypto.cpp in EQMacEmu).

#include <cstddef>
#include <cstdint>

namespace Des
{
	// length must be a multiple of 8.
	void CbcEncrypt(const uint8_t key[8], const uint8_t iv[8], const uint8_t *in, uint8_t *out, size_t length);
	void CbcDecrypt(const uint8_t key[8], const uint8_t iv[8], const uint8_t *in, uint8_t *out, size_t length);

	// Known-answer self test (FIPS 81 style vector). Returns true when the implementation is correct.
	bool SelfTest();
}
