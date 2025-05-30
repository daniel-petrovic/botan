/*
* ECC Key implemenation
* (C) 2007 Manuel Hartl, FlexSecure GmbH
*          Falko Strenzke, FlexSecure GmbH
*     2008-2010 Jack Lloyd
*
* Botan is released under the Simplified BSD License (see license.txt)
*/

#include <botan/ecc_key.h>
#include <botan/numthry.h>
#include <botan/der_enc.h>
#include <botan/ber_dec.h>
#include <botan/secmem.h>
#include <botan/ec_point.h>
#include <botan/internal/workfactor.h>

namespace Botan {

size_t EC_PublicKey::key_length() const
   {
   return domain().get_p_bits();
   }

size_t EC_PublicKey::estimated_strength() const
   {
   return ecp_work_factor(key_length());
   }

namespace {

EC_Group_Encoding default_encoding_for(EC_Group& group)
   {
   if(group.get_curve_oid().empty())
      return EC_Group_Encoding::Explicit;
   else
      return EC_Group_Encoding::NamedCurve;
   }

}

EC_PublicKey::EC_PublicKey(const EC_Group& dom_par,
                           const EC_Point& pub_point) :
   m_domain_params(dom_par),
   m_public_key(pub_point),
   m_domain_encoding(default_encoding_for(m_domain_params))
   {
#if 0
   if(domain().get_curve() != public_point().get_curve())
      throw Invalid_Argument("EC_PublicKey: curve mismatch in constructor");
#endif
   }

EC_PublicKey::EC_PublicKey(const AlgorithmIdentifier& alg_id,
                           const std::vector<uint8_t>& key_bits) :
   m_domain_params{EC_Group(alg_id.get_parameters())},
   m_public_key{domain().OS2ECP(key_bits)},
   m_domain_encoding(default_encoding_for(m_domain_params))
   {
   }

bool EC_PublicKey::check_key(RandomNumberGenerator& rng,
                             bool /*strong*/) const
   {
   return m_domain_params.verify_group(rng) &&
          m_domain_params.verify_public_element(public_point());
   }


AlgorithmIdentifier EC_PublicKey::algorithm_identifier() const
   {
   return AlgorithmIdentifier(get_oid(), DER_domain());
   }

std::vector<uint8_t> EC_PublicKey::public_key_bits() const
   {
   return public_point().encode(point_encoding());
   }

void EC_PublicKey::set_point_encoding(EC_Point::Compression_Type enc)
   {
   if(enc != EC_Point::COMPRESSED &&
      enc != EC_Point::UNCOMPRESSED &&
      enc != EC_Point::HYBRID)
      throw Invalid_Argument("Invalid point encoding for EC_PublicKey");

   m_point_encoding = enc;
   }

void EC_PublicKey::set_parameter_encoding(EC_Group_Encoding form)
   {
   if(form == EC_Group_Encoding::NamedCurve && m_domain_params.get_curve_oid().empty())
      throw Invalid_Argument("Cannot used NamedCurve encoding for a curve without an OID");

   m_domain_encoding = form;
   }

const BigInt& EC_PrivateKey::private_value() const
   {
   if(m_private_key == 0)
      throw Invalid_State("EC_PrivateKey::private_value - uninitialized");

   return m_private_key;
   }

/**
* EC_PrivateKey constructor
*/
EC_PrivateKey::EC_PrivateKey(RandomNumberGenerator& rng,
                             const EC_Group& ec_group,
                             const BigInt& x,
                             bool with_modular_inverse)
   {
   m_domain_params = ec_group;
   m_domain_encoding = default_encoding_for(m_domain_params);

   if(x == 0)
      {
      m_private_key = ec_group.random_scalar(rng);
      }
   else
      {
      m_private_key = x;
      }

   std::vector<BigInt> ws;

   if(with_modular_inverse)
      {
      // ECKCDSA
      m_public_key = domain().blinded_base_point_multiply(
         m_domain_params.inverse_mod_order(m_private_key), rng, ws);
      }
   else
      {
      m_public_key = domain().blinded_base_point_multiply(m_private_key, rng, ws);
      }

   BOTAN_ASSERT(m_public_key.on_the_curve(),
                "Generated public key point was on the curve");
   }

secure_vector<uint8_t> EC_PrivateKey::private_key_bits() const
   {
   return DER_Encoder()
      .start_sequence()
         .encode(static_cast<size_t>(1))
         .encode(BigInt::encode_1363(m_private_key, m_private_key.bytes()), ASN1_Type::OctetString)
         .start_explicit_context_specific(1)
            .encode(m_public_key.encode(EC_Point::Compression_Type::UNCOMPRESSED), ASN1_Type::BitString)
         .end_cons()
      .end_cons()
      .get_contents();
   }

EC_PrivateKey::EC_PrivateKey(const AlgorithmIdentifier& alg_id,
                             const secure_vector<uint8_t>& key_bits,
                             bool with_modular_inverse)
   {
   m_domain_params = EC_Group(alg_id.get_parameters());
   m_domain_encoding = default_encoding_for(m_domain_params);

   OID key_parameters;
   secure_vector<uint8_t> public_key_bits;

   BER_Decoder(key_bits)
      .start_sequence()
         .decode_and_check<size_t>(1, "Unknown version code for ECC key")
         .decode_octet_string_bigint(m_private_key)
         .decode_optional(key_parameters, ASN1_Type(0), ASN1_Class::ExplicitContextSpecific)
         .decode_optional_string(public_key_bits, ASN1_Type::BitString, 1, ASN1_Class::ExplicitContextSpecific)
      .end_cons();

   if(public_key_bits.empty())
      {
      if(with_modular_inverse)
         {
         // ECKCDSA
         m_public_key = domain().get_base_point() * m_domain_params.inverse_mod_order(m_private_key);
         }
      else
         {
         m_public_key = domain().get_base_point() * m_private_key;
         }

      BOTAN_ASSERT(m_public_key.on_the_curve(),
                   "Public point derived from loaded key was on the curve");
      }
   else
      {
      m_public_key = domain().OS2ECP(public_key_bits);
      // OS2ECP verifies that the point is on the curve
      }
   }

}
