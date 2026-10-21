Run `python generate.py` with Python's `cryptography` package installed to
replace `tokens.json` with fresh test signatures. The generator creates
2048-bit RSA keys in memory and writes only the public key, test tokens and
the private proof key whose public key every token's nonce names. Accepted
tokens expire in 2100. The other tokens exercise claim, header and signature
rejection.
