#pragma once
#include "../storage.hpp"
#include "../portable/identity_format.hpp"
#include <mbedtls/sha256.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/entropy.h>
#include <mbedtls/platform_util.h>
#include <stdexcept>

namespace peerchat::platform::portable_crypto {
inline void checked(int result){if(result!=0)throw std::runtime_error("Portable cryptographic operation failed");}
inline Blob sha256(const Blob& value){Blob digest(32);checked(mbedtls_sha256(value.data(),value.size(),digest.data(),0));return digest;}
inline void wipe(void* value,size_t length){mbedtls_platform_zeroize(value,length);}
inline Blob random(size_t length){
    if(length>32)throw std::runtime_error("Invalid random request");
    struct Entropy {mbedtls_entropy_context value;Entropy(){mbedtls_entropy_init(&value);}~Entropy(){mbedtls_entropy_free(&value);}} entropy;
    Blob result(length);checked(mbedtls_entropy_func(&entropy.value,result.data(),result.size()));return result;
}
inline bool verify(const Blob& payload,const Blob& signature,const Blob& key){
    if(signature.size()!=64||!identity_format::validPublicRepresentation(key.data(),key.size()))return false;
    struct Curve {mbedtls_ecp_group group;mbedtls_ecp_point point;mbedtls_mpi r,s;
        Curve(){mbedtls_ecp_group_init(&group);mbedtls_ecp_point_init(&point);mbedtls_mpi_init(&r);mbedtls_mpi_init(&s);}
        ~Curve(){mbedtls_mpi_free(&s);mbedtls_mpi_free(&r);mbedtls_ecp_point_free(&point);mbedtls_ecp_group_free(&group);}} curve;
    try{checked(mbedtls_ecp_group_load(&curve.group,MBEDTLS_ECP_DP_SECP256R1));
        Blob point{4};point.insert(point.end(),key.begin()+8,key.end());
        checked(mbedtls_ecp_point_read_binary(&curve.group,&curve.point,point.data(),point.size()));
        checked(mbedtls_ecp_check_pubkey(&curve.group,&curve.point));
        checked(mbedtls_mpi_read_binary(&curve.r,signature.data(),32));checked(mbedtls_mpi_read_binary(&curve.s,signature.data()+32,32));
        auto digest=sha256(payload);return mbedtls_ecdsa_verify(&curve.group,digest.data(),digest.size(),&curve.point,&curve.r,&curve.s)==0;
    }catch(...){return false;}
}
}
