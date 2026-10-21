import base64
import hashlib
import json
from pathlib import Path

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import padding, rsa


def encoded(value):
    return base64.urlsafe_b64encode(value).decode().rstrip("=")


def signed(claims, header=None, signing_key=None):
    header = header or {"alg": "RS256", "kid": "fixture-rsa"}
    parts = [encoded(json.dumps(value, separators=(",", ":")).encode())
             for value in (header, claims)]
    message = ".".join(parts).encode()
    signature = (signing_key or key).sign(
        message, padding.PKCS1v15(), hashes.SHA256())
    return message.decode() + "." + encoded(signature)


key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
public = key.public_key().public_numbers()
number = lambda value: encoded(
    value.to_bytes((value.bit_length() + 7) // 8, "big"))
jwks = {"keys": [{"kid": "fixture-rsa", "kty": "RSA", "alg": "RS256",
                   "use": "sig", "n": number(public.n),
                   "e": number(public.e)}]}
proof = rsa.generate_private_key(public_exponent=65537, key_size=2048)
proof_public = proof.public_key().public_bytes(
    serialization.Encoding.PEM,
    serialization.PublicFormat.SubjectPublicKeyInfo).decode()
proof_private = proof.private_bytes(
    serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
    serialization.NoEncryption()).decode()
claims = {"iss": "https://discord.com", "aud": "1561065725962092666",
          "nonce": hashlib.sha256(proof_public.encode()).hexdigest(),
          "sub": "123456789012345678", "iat": 1, "exp": 4102444800}
tokens = {"valid": signed(claims)}
for name, change in {
    "name": {"name": "Fixture Player"},
    "nonce": {"nonce": "another-host"},
    "audience": {"aud": "another-application"},
    "audience_type": {"aud": 1561065725962092666},
    "audience_untrusted": {"aud": [claims["aud"], "another-application"],
                           "azp": claims["aud"]},
    "audience_wrong_party": {"azp": "another-application"},
    "issuer": {"iss": "https://discord.com/"},
    "subject_empty": {"sub": ""},
}.items():
    tokens[name] = signed(claims | change)
tokens["missing_exp"] = signed(
    {k: v for k, v in claims.items() if k != "exp"})
for name, change in {
    "algorithm": {"alg": "HS256"},
    "unsigned": {"alg": "none"},
    "unknown_key": {"kid": "rotated-key"},
    "critical": {"crit": ["extension"]},
}.items():
    tokens[name] = signed(claims, {"alg": "RS256", "kid": "fixture-rsa"}
                          | change)
other = rsa.generate_private_key(public_exponent=65537, key_size=2048)
tokens["signature"] = signed(claims, signing_key=other)
parts = tokens["valid"].split(".")
tokens["tampered"] = parts[0] + "." + encoded(
    json.dumps(claims | {"sub": "999"}, separators=(",", ":")).encode()
) + "." + parts[2]
tokens["malformed_json"] = encoded(b"not json") + "." + ".".join(parts[1:])
Path(__file__).with_name("tokens.json").write_text(
    json.dumps({"jwks": jwks, "proof_key": proof_private, "tokens": tokens},
               indent=2) + "\n")
