// Aleth: Ethereum C++ client, tools and libraries.
// Copyright 2019 Aleth Authors.
// Licensed under the GNU General Public License, Version 3.

#include "ENR.h"
#include <libdevcore/SHA3.h>

static std::string const c_keyID = "id";
static std::string const c_keySec256k1 = "sec256k1";
static std::string const c_keyIP = "ip";
static std::string const c_keyTCP = "tcp";
static std::string const c_keyUDP = "udp";

namespace dev
{
namespace p2p
{
namespace
{
    template<class Address>
    bytes addressToBytes(Address const& _address)
    {
        auto const addressBytes = _address.to_bytes();
        return bytes(addressBytes.begin(), addressBytes.end());
    }    
}  // namespace

ENR::ENR(RLP _rlp, VerifyFunction _verifyFunction)
{
    // TODO fail if > 300 bytes

    m_signature = _rlp[0].toBytes(RLP::VeryStrict);

    m_seq = _rlp[1].toInt<uint64_t>(RLP::VeryStrict);
    // TODO check order
    for (size_t i = 2; i < _rlp.itemCount(); i+= 2)
    {
        auto key = _rlp[i].toString(RLP::VeryStrict);
        auto value = _rlp[i + 1].data().toBytes();
        m_map.insert({key, value});
    }

    if (!_verifyFunction(m_map, dev::ref(m_signature), dev::ref(content())))
        BOOST_THROW_EXCEPTION(ENRSignatureIsInvalid());
}

ENR::ENR(uint64_t _seq, std::map<std::string, bytes> const& _keyValues, SignFunction _signFunction)
  : m_seq(_seq), m_map(_keyValues), m_signature(_signFunction(dev::ref(content())))
{
}

bytes ENR::content() const
{
    RLPStream stream(contentListSize());
    streamContent(stream);
    return stream.out();
}


void ENR::streamRLP(RLPStream& _s) const
{
    _s.appendList(contentListSize() + 1);
    _s << m_signature;
    streamContent(_s);
}

void ENR::streamContent(RLPStream& _s) const
{
    _s << m_seq;
    for (auto const keyValue : m_map)
    {
        _s << keyValue.first;
        _s.appendRaw(keyValue.second);
    }
}

ENR createV4ENR(Secret const& _secret, boost::asio::ip::address const& _ip, uint16_t _tcpPort,  uint16_t _udpPort)
{
    ENR::SignFunction signFunction = [&_secret](bytesConstRef _data) { 
        Signature s = dev::sign(_secret, sha3(_data)); 
        // The resulting 64-byte signature is encoded as the concatenation of the r and s signature values.
        return bytes(s.data(), s.data() + 64);
    };

    PublicCompressed publicKey = toPublicCompressed(_secret);
    
    auto const address = _ip.is_v4() ? addressToBytes(_ip.to_v4()) : addressToBytes(_ip.to_v6());

    std::map<std::string, bytes> keyValues = {{c_keyID, rlp(bytes{'v', '4'})},
        {c_keySec256k1, rlp(publicKey.asBytes())}, {c_keyIP, rlp(address)},
        {c_keyTCP, rlp(_tcpPort)}, {c_keyUDP, rlp(_udpPort)}};

    return ENR{0, keyValues, signFunction};
}

ENR parseV4ENR(RLP _rlp)
{
    ENR::VerifyFunction verifyFunction = [](std::map<std::string, bytes> const& _keyValues,
                                             bytesConstRef _signature, bytesConstRef _data) {
        auto itKey = _keyValues.find(c_keySec256k1);
        if (itKey == _keyValues.end())
            return false;

        auto const key = RLP(itKey->second).toHash<PublicCompressed>(RLP::VeryStrict);
        h512 const signature{_signature};

        return verify(key, signature, sha3(_data));
    };

    return ENR{_rlp, verifyFunction};
}

std::ostream& operator<<(std::ostream& _out, ENR const& _enr)
{
    RLPStream s;
    _enr.streamRLP(s);

    _out << RLP(s.out());
    return _out;
}

}  // namespace p2p
}  // namespace dev
