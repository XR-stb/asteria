// This file is part of Asteria.
// Copyleft 2018 - 2021, LH_Mouse. All wrongs reserved.

#include "../precompiled.hpp"
#include "checksum.hpp"
#include "../runtime/argument_reader.hpp"
#include "../runtime/runtime_error.hpp"
#include "../runtime/global_context.hpp"
#include "../utils.hpp"
#include <sys/stat.h>
#include <openssl/md5.h>
#include <openssl/sha.h>
#include <zlib.h>

namespace asteria {
namespace {

class CRC32_Hasher final
  : public Abstract_Opaque
  {
  private:
    ::uLong m_reg;

  public:
    explicit
    CRC32_Hasher() noexcept
      { this->clear();  }

  public:
    tinyfmt&
    describe(tinyfmt& fmt) const override
      { return fmt << "instance of `std.checksum.CRC32` at `" << this << "`";  }

    Variable_Callback&
    enumerate_variables(Variable_Callback& callback) const override
      { return callback;  }

    CRC32_Hasher*
    clone_opt(rcptr<Abstract_Opaque>& output) const override
      { return clone_opaque(output, *this);  }

    void
    clear() noexcept
      {
        this->m_reg = 0;
      }

    void
    update(const void* data, size_t size) noexcept
      {
        auto bptr = static_cast<const ::Bytef*>(data);
#if ZLIB_VERNUM < 0x1290
        // XXX: Why do we have to maintain this stupid piece of code for compatibility?
        const auto eptr = bptr + size;
        while(ptrdiff_t n = ::rocket::min(eptr - bptr, INT_MAX))
          this->m_reg = ::crc32(this->m_reg, (bptr += n) - n, static_cast<::uInt>(n));
#else
        // zlib >= 1.2.9
        this->m_reg = ::crc32_z(this->m_reg, bptr, size);
#endif
      }

    V_integer
    finish() noexcept
      {
        V_integer ck = static_cast<uint32_t>(this->m_reg);
        this->clear();
        return ck;
      }
  };

void
do_construct_CRC32(V_object& result)
  {
    static constexpr auto s_uuid = sref("{2C78B9D8-A8F4-4CE9-36E7-12B9EE14AD3D}");
    result.insert_or_assign(s_uuid, std_checksum_CRC32_private());

    result.insert_or_assign(sref("update"),
      ASTERIA_BINDING_BEGIN("std.checksum.CRC32::update", self, global, reader) {
        self.push_modifier_object_key(s_uuid);
        auto& hasher = self.dereference_mutable().open_opaque();
        self.pop_modifier();
        V_string data;

        reader.start_overload();
        reader.required(data);    // data
        if(reader.end_overload())
          ASTERIA_BINDING_RETURN_MOVE(self,
                    std_checksum_CRC32_update, ::std::ref(hasher), data);
      }
      ASTERIA_BINDING_END);

    result.insert_or_assign(sref("finish"),
      ASTERIA_BINDING_BEGIN("std.checksum.CRC32::finish", self, global, reader) {
        self.push_modifier_object_key(s_uuid);
        auto& hasher = self.dereference_mutable().open_opaque();
        self.pop_modifier();

        reader.start_overload();
        if(reader.end_overload())
          ASTERIA_BINDING_RETURN_MOVE(self,
                    std_checksum_CRC32_finish, ::std::ref(hasher));
      }
      ASTERIA_BINDING_END);

    result.insert_or_assign(sref("clear"),
      ASTERIA_BINDING_BEGIN("std.checksum.CRC32::clear", self, global, reader) {
        self.push_modifier_object_key(s_uuid);
        auto& hasher = self.dereference_mutable().open_opaque();
        self.pop_modifier();

        reader.start_overload();
        if(reader.end_overload())
          ASTERIA_BINDING_RETURN_MOVE(self,
                    std_checksum_CRC32_clear, ::std::ref(hasher));
      }
      ASTERIA_BINDING_END);
  }

class FNV1a32_Hasher final
  : public Abstract_Opaque
  {
  private:
    uint32_t m_reg;

  public:
    explicit
    FNV1a32_Hasher() noexcept
      { this->clear();  }

  public:
    tinyfmt&
    describe(tinyfmt& fmt) const override
      { return fmt << "instance of `std.checksum.FNV1a32` at `" << this << "`";  }

    Variable_Callback&
    enumerate_variables(Variable_Callback& callback) const override
      { return callback;  }

    FNV1a32_Hasher*
    clone_opt(rcptr<Abstract_Opaque>& output) const override
      { return clone_opaque(output, *this);  }

    void
    clear() noexcept
      {
        this->m_reg = 2166136261;
      }

    void
    update(const void* data, size_t size) noexcept
      {
        auto bptr = static_cast<const uint8_t*>(data);
        auto eptr = bptr + size;
        while(bptr != eptr)
          this->m_reg = (this->m_reg ^ *(bptr++)) * 16777619;
      }

    V_integer
    finish() noexcept
      {
        V_integer ck = static_cast<uint32_t>(this->m_reg);
        this->clear();
        return ck;
      }
  };

void
do_construct_FNV1a32(V_object& result)
  {
    static constexpr auto s_uuid = sref("{2C79571C-5D7B-4674-056A-6C0D075A82FC}");
    result.insert_or_assign(s_uuid, std_checksum_FNV1a32_private());

    result.insert_or_assign(sref("update"),
      ASTERIA_BINDING_BEGIN("std.checksum.FNV1a32::update", self, global, reader) {
        self.push_modifier_object_key(s_uuid);
        auto& hasher = self.dereference_mutable().open_opaque();
        self.pop_modifier();
        V_string data;

        reader.start_overload();
        reader.required(data);    // data
        if(reader.end_overload())
          ASTERIA_BINDING_RETURN_MOVE(self,
                    std_checksum_FNV1a32_update, ::std::ref(hasher), data);
      }
      ASTERIA_BINDING_END);

    result.insert_or_assign(sref("finish"),
      ASTERIA_BINDING_BEGIN("std.checksum.FNV1a32::finish", self, global, reader) {
        self.push_modifier_object_key(s_uuid);
        auto& hasher = self.dereference_mutable().open_opaque();
        self.pop_modifier();

        reader.start_overload();
        if(reader.end_overload())
          ASTERIA_BINDING_RETURN_MOVE(self,
                    std_checksum_FNV1a32_finish, ::std::ref(hasher));
      }
      ASTERIA_BINDING_END);

    result.insert_or_assign(sref("clear"),
      ASTERIA_BINDING_BEGIN("std.checksum.FNV1a32::clear", self, global, reader) {
        self.push_modifier_object_key(s_uuid);
        auto& hasher = self.dereference_mutable().open_opaque();
        self.pop_modifier();

        reader.start_overload();
        if(reader.end_overload())
          ASTERIA_BINDING_RETURN_MOVE(self,
                    std_checksum_FNV1a32_clear, ::std::ref(hasher));
      }
      ASTERIA_BINDING_END);
  }

V_string
do_copy_SHA_result(const unsigned char* bytes, size_t size)
  {
    V_string str;
    auto wpos = str.insert(str.end(), size * 2, '/');

    for(size_t k = 0;  k != size;  ++k) {
      uint32_t ch = (bytes[k] >> 4) & 0x0F;
      *(wpos++) = static_cast<char>('0' + ch + ((9 - ch) >> 29));
      ch = bytes[k] & 0x0F;
      *(wpos++) = static_cast<char>('0' + ch + ((9 - ch) >> 29));
    }
    return str;
  }

class MD5_Hasher final
  : public Abstract_Opaque
  {
  private:
    ::MD5_CTX m_reg[1];

  public:
    explicit
    MD5_Hasher() noexcept
      { this->clear();  }

  public:
    tinyfmt&
    describe(tinyfmt& fmt) const override
      { return fmt << "instance of `std.checksum.MD5` at `" << this << "`";  }

    Variable_Callback&
    enumerate_variables(Variable_Callback& callback) const override
      { return callback;  }

    MD5_Hasher*
    clone_opt(rcptr<Abstract_Opaque>& output) const override
      { return clone_opaque(output, *this);  }

    void
    clear() noexcept
      {
        ::MD5_Init(this->m_reg);
      }

    void
    update(const void* data, size_t size) noexcept
      {
        ::MD5_Update(this->m_reg, data, size);
      }

    V_string
    finish() noexcept
      {
        unsigned char bytes[MD5_DIGEST_LENGTH];
        ::MD5_Final(bytes, this->m_reg);
        V_string ck = do_copy_SHA_result(bytes, sizeof(bytes));
        this->clear();
        return ck;
      }
  };

void
do_construct_MD5(V_object& result)
  {
    static constexpr auto s_uuid = sref("{2C795808-7290-4675-056A-D3825905F8E1}");
    result.insert_or_assign(s_uuid, std_checksum_MD5_private());

    result.insert_or_assign(sref("update"),
      ASTERIA_BINDING_BEGIN("std.checksum.MD5::update", self, global, reader) {
        self.push_modifier_object_key(s_uuid);
        auto& hasher = self.dereference_mutable().open_opaque();
        self.pop_modifier();
        V_string data;

        reader.start_overload();
        reader.required(data);    // data
        if(reader.end_overload())
          ASTERIA_BINDING_RETURN_MOVE(self,
                    std_checksum_MD5_update, ::std::ref(hasher), data);
      }
      ASTERIA_BINDING_END);

    result.insert_or_assign(sref("finish"),
      ASTERIA_BINDING_BEGIN("std.checksum.MD5::finish", self, global, reader) {
        self.push_modifier_object_key(s_uuid);
        auto& hasher = self.dereference_mutable().open_opaque();
        self.pop_modifier();

        reader.start_overload();
        if(reader.end_overload())
          ASTERIA_BINDING_RETURN_MOVE(self,
                    std_checksum_MD5_finish, ::std::ref(hasher));
      }
      ASTERIA_BINDING_END);

    result.insert_or_assign(sref("clear"),
      ASTERIA_BINDING_BEGIN("std.checksum.MD5::clear", self, global, reader) {
        self.push_modifier_object_key(s_uuid);
        auto& hasher = self.dereference_mutable().open_opaque();
        self.pop_modifier();

        reader.start_overload();
        if(reader.end_overload())
          ASTERIA_BINDING_RETURN_MOVE(self,
                    std_checksum_MD5_clear, ::std::ref(hasher));
      }
      ASTERIA_BINDING_END);
  }

class SHA1_Hasher final
  : public Abstract_Opaque
  {
  private:
    ::SHA_CTX m_reg[1];

  public:
    explicit
    SHA1_Hasher() noexcept
      { this->clear();  }

  public:
    tinyfmt&
    describe(tinyfmt& fmt) const override
      { return fmt << "instance of `std.checksum.SHA1` at `" << this << "`";  }

    Variable_Callback&
    enumerate_variables(Variable_Callback& callback) const override
      { return callback;  }

    SHA1_Hasher*
    clone_opt(rcptr<Abstract_Opaque>& output) const override
      { return clone_opaque(output, *this);  }

    void
    clear() noexcept
      {
        ::SHA1_Init(this->m_reg);
      }

    void
    update(const void* data, size_t size) noexcept
      {
        ::SHA1_Update(this->m_reg, data, size);
      }

    V_string
    finish() noexcept
      {
        unsigned char bytes[SHA_DIGEST_LENGTH];
        ::SHA1_Final(bytes, this->m_reg);
        V_string ck = do_copy_SHA_result(bytes, sizeof(bytes));
        this->clear();
        return ck;
      }
  };

void
do_construct_SHA1(V_object& result)
  {
    static constexpr auto s_uuid = sref("{2D242315-AF9A-4EDC-0612-CBBBCBBB75BB}");
    result.insert_or_assign(s_uuid, std_checksum_SHA1_private());

    result.insert_or_assign(sref("update"),
      ASTERIA_BINDING_BEGIN("std.checksum.SHA1::update", self, global, reader) {
        self.push_modifier_object_key(s_uuid);
        auto& hasher = self.dereference_mutable().open_opaque();
        self.pop_modifier();
        V_string data;

        reader.start_overload();
        reader.required(data);    // data
        if(reader.end_overload())
          ASTERIA_BINDING_RETURN_MOVE(self,
                    std_checksum_SHA1_update, ::std::ref(hasher), data);
      }
      ASTERIA_BINDING_END);

    result.insert_or_assign(sref("finish"),
      ASTERIA_BINDING_BEGIN("std.checksum.SHA1::finish", self, global, reader) {
        self.push_modifier_object_key(s_uuid);
        auto& hasher = self.dereference_mutable().open_opaque();
        self.pop_modifier();

        reader.start_overload();
        if(reader.end_overload())
          ASTERIA_BINDING_RETURN_MOVE(self,
                    std_checksum_SHA1_finish, ::std::ref(hasher));
      }
      ASTERIA_BINDING_END);

    result.insert_or_assign(sref("clear"),
      ASTERIA_BINDING_BEGIN("std.checksum.SHA1::clear", self, global, reader) {
        self.push_modifier_object_key(s_uuid);
        auto& hasher = self.dereference_mutable().open_opaque();
        self.pop_modifier();

        reader.start_overload();
        if(reader.end_overload())
          ASTERIA_BINDING_RETURN_MOVE(self,
                    std_checksum_SHA1_clear, ::std::ref(hasher));
      }
      ASTERIA_BINDING_END);
  }

class SHA224_Hasher final
  : public Abstract_Opaque
  {
  private:
    ::SHA256_CTX m_reg[1];

  public:
    explicit
    SHA224_Hasher() noexcept
      { this->clear();  }

  public:
    tinyfmt&
    describe(tinyfmt& fmt) const override
      { return fmt << "instance of `std.checksum.SHA224` at `" << this << "`";  }

    Variable_Callback&
    enumerate_variables(Variable_Callback& callback) const override
      { return callback;  }

    SHA224_Hasher*
    clone_opt(rcptr<Abstract_Opaque>& output) const override
      { return clone_opaque(output, *this);  }

    void
    clear() noexcept
      {
        ::SHA224_Init(this->m_reg);
      }

    void
    update(const void* data, size_t size) noexcept
      {
        ::SHA224_Update(this->m_reg, data, size);
      }

    V_string
    finish() noexcept
      {
        unsigned char bytes[SHA224_DIGEST_LENGTH];
        ::SHA224_Final(bytes, this->m_reg);
        V_string ck = do_copy_SHA_result(bytes, sizeof(bytes));
        this->clear();
        return ck;
      }
  };

void
do_construct_SHA224(V_object& result)
  {
    static constexpr auto s_uuid = sref("{2D24231A-8D6F-4EDC-0612-C448C44886E4}");
    result.insert_or_assign(s_uuid, std_checksum_SHA224_private());

    result.insert_or_assign(sref("update"),
      ASTERIA_BINDING_BEGIN("std.checksum.SHA224::update", self, global, reader) {
        self.push_modifier_object_key(s_uuid);
        auto& hasher = self.dereference_mutable().open_opaque();
        self.pop_modifier();
        V_string data;

        reader.start_overload();
        reader.required(data);    // data
        if(reader.end_overload())
          ASTERIA_BINDING_RETURN_MOVE(self,
                    std_checksum_SHA224_update, ::std::ref(hasher), data);
      }
      ASTERIA_BINDING_END);

    result.insert_or_assign(sref("finish"),
      ASTERIA_BINDING_BEGIN("std.checksum.SHA224::finish", self, global, reader) {
        self.push_modifier_object_key(s_uuid);
        auto& hasher = self.dereference_mutable().open_opaque();
        self.pop_modifier();

        reader.start_overload();
        if(reader.end_overload())
          ASTERIA_BINDING_RETURN_MOVE(self,
                    std_checksum_SHA224_finish, ::std::ref(hasher));
      }
      ASTERIA_BINDING_END);

    result.insert_or_assign(sref("clear"),
      ASTERIA_BINDING_BEGIN("std.checksum.SHA224::clear", self, global, reader) {
        self.push_modifier_object_key(s_uuid);
        auto& hasher = self.dereference_mutable().open_opaque();
        self.pop_modifier();

        reader.start_overload();
        if(reader.end_overload())
          ASTERIA_BINDING_RETURN_MOVE(self,
                    std_checksum_SHA224_clear, ::std::ref(hasher));
      }
      ASTERIA_BINDING_END);
  }

class SHA256_Hasher final
  : public Abstract_Opaque
  {
  private:
    ::SHA256_CTX m_reg[1];

  public:
    explicit
    SHA256_Hasher() noexcept
      { this->clear();  }

  public:
    tinyfmt&
    describe(tinyfmt& fmt) const override
      { return fmt << "instance of `std.checksum.SHA256` at `" << this << "`";  }

    Variable_Callback&
    enumerate_variables(Variable_Callback& callback) const override
      { return callback;  }

    SHA256_Hasher*
    clone_opt(rcptr<Abstract_Opaque>& output) const override
      { return clone_opaque(output, *this);  }

    void
    clear() noexcept
      {
        ::SHA256_Init(this->m_reg);
      }

    void
    update(const void* data, size_t size) noexcept
      {
        ::SHA256_Update(this->m_reg, data, size);
      }

    V_string
    finish() noexcept
      {
        unsigned char bytes[SHA256_DIGEST_LENGTH];
        ::SHA256_Final(bytes, this->m_reg);
        V_string ck = do_copy_SHA_result(bytes, sizeof(bytes));
        this->clear();
        return ck;
      }
  };

void
do_construct_SHA256(V_object& result)
  {
    static constexpr auto s_uuid = sref("{2D24231C-F3D7-4EDC-0612-551055107FE2}");
    result.insert_or_assign(s_uuid, std_checksum_SHA256_private());

    result.insert_or_assign(sref("update"),
      ASTERIA_BINDING_BEGIN("std.checksum.SHA256::update", self, global, reader) {
        self.push_modifier_object_key(s_uuid);
        auto& hasher = self.dereference_mutable().open_opaque();
        self.pop_modifier();
        V_string data;

        reader.start_overload();
        reader.required(data);    // data
        if(reader.end_overload())
          ASTERIA_BINDING_RETURN_MOVE(self,
                    std_checksum_SHA256_update, ::std::ref(hasher), data);
      }
      ASTERIA_BINDING_END);

    result.insert_or_assign(sref("finish"),
      ASTERIA_BINDING_BEGIN("std.checksum.SHA256::finish", self, global, reader) {
        self.push_modifier_object_key(s_uuid);
        auto& hasher = self.dereference_mutable().open_opaque();
        self.pop_modifier();

        reader.start_overload();
        if(reader.end_overload())
          ASTERIA_BINDING_RETURN_MOVE(self,
                    std_checksum_SHA256_finish, ::std::ref(hasher));
      }
      ASTERIA_BINDING_END);

    result.insert_or_assign(sref("clear"),
      ASTERIA_BINDING_BEGIN("std.checksum.SHA256::clear", self, global, reader) {
        self.push_modifier_object_key(s_uuid);
        auto& hasher = self.dereference_mutable().open_opaque();
        self.pop_modifier();

        reader.start_overload();
        if(reader.end_overload())
          ASTERIA_BINDING_RETURN_MOVE(self,
                    std_checksum_SHA256_clear, ::std::ref(hasher));
      }
      ASTERIA_BINDING_END);
  }

class SHA384_Hasher final
  : public Abstract_Opaque
  {
  private:
    ::SHA512_CTX m_reg[1];

  public:
    explicit
    SHA384_Hasher() noexcept
      { this->clear();  }

  public:
    tinyfmt&
    describe(tinyfmt& fmt) const override
      { return fmt << "instance of `std.checksum.SHA384` at `" << this << "`";  }

    Variable_Callback&
    enumerate_variables(Variable_Callback& callback) const override
      { return callback;  }

    SHA384_Hasher*
    clone_opt(rcptr<Abstract_Opaque>& output) const override
      { return clone_opaque(output, *this);  }

    void
    clear() noexcept
      {
        ::SHA384_Init(this->m_reg);
      }

    void
    update(const void* data, size_t size) noexcept
      {
        ::SHA384_Update(this->m_reg, data, size);
      }

    V_string
    finish() noexcept
      {
        unsigned char bytes[SHA384_DIGEST_LENGTH];
        ::SHA384_Final(bytes, this->m_reg);
        V_string ck = do_copy_SHA_result(bytes, sizeof(bytes));
        this->clear();
        return ck;
      }
  };

void
do_construct_SHA384(V_object& result)
  {
    static constexpr auto s_uuid = sref("{2D24231E-B48F-4EDC-0612-145E145E6F29}");
    result.insert_or_assign(s_uuid, std_checksum_SHA384_private());

    result.insert_or_assign(sref("update"),
      ASTERIA_BINDING_BEGIN("std.checksum.SHA384::update", self, global, reader) {
        self.push_modifier_object_key(s_uuid);
        auto& hasher = self.dereference_mutable().open_opaque();
        self.pop_modifier();
        V_string data;

        reader.start_overload();
        reader.required(data);    // data
        if(reader.end_overload())
          ASTERIA_BINDING_RETURN_MOVE(self,
                    std_checksum_SHA384_update, ::std::ref(hasher), data);
      }
      ASTERIA_BINDING_END);

    result.insert_or_assign(sref("finish"),
      ASTERIA_BINDING_BEGIN("std.checksum.SHA384::finish", self, global, reader) {
        self.push_modifier_object_key(s_uuid);
        auto& hasher = self.dereference_mutable().open_opaque();
        self.pop_modifier();

        reader.start_overload();
        if(reader.end_overload())
          ASTERIA_BINDING_RETURN_MOVE(self,
                    std_checksum_SHA384_finish, ::std::ref(hasher));
      }
      ASTERIA_BINDING_END);

    result.insert_or_assign(sref("clear"),
      ASTERIA_BINDING_BEGIN("std.checksum.SHA384::clear", self, global, reader) {
        self.push_modifier_object_key(s_uuid);
        auto& hasher = self.dereference_mutable().open_opaque();
        self.pop_modifier();

        reader.start_overload();
        if(reader.end_overload())
          ASTERIA_BINDING_RETURN_MOVE(self,
                    std_checksum_SHA384_clear, ::std::ref(hasher));
      }
      ASTERIA_BINDING_END);
  }

class SHA512_Hasher final
  : public Abstract_Opaque
  {
  private:
    ::SHA512_CTX m_reg[1];

  public:
    explicit
    SHA512_Hasher() noexcept
      { this->clear();  }

  public:
    tinyfmt&
    describe(tinyfmt& fmt) const override
      { return fmt << "instance of `std.checksum.SHA512` at `" << this << "`";  }

    Variable_Callback&
    enumerate_variables(Variable_Callback& callback) const override
      { return callback;  }

    SHA512_Hasher*
    clone_opt(rcptr<Abstract_Opaque>& output) const override
      { return clone_opaque(output, *this);  }

    void
    clear() noexcept
      {
        ::SHA512_Init(this->m_reg);
      }

    void
    update(const void* data, size_t size) noexcept
      {
        ::SHA512_Update(this->m_reg, data, size);
      }

    V_string
    finish() noexcept
      {
        unsigned char bytes[SHA512_DIGEST_LENGTH];
        ::SHA512_Final(bytes, this->m_reg);
        V_string ck = do_copy_SHA_result(bytes, sizeof(bytes));
        this->clear();
        return ck;
      }
  };

void
do_construct_SHA512(V_object& result)
  {
    static constexpr auto s_uuid = sref("{2D242320-7A94-4EDC-0612-8851885187F8}");
    result.insert_or_assign(s_uuid, std_checksum_SHA512_private());

    result.insert_or_assign(sref("update"),
      ASTERIA_BINDING_BEGIN("std.checksum.SHA512::update", self, global, reader) {
        self.push_modifier_object_key(s_uuid);
        auto& hasher = self.dereference_mutable().open_opaque();
        self.pop_modifier();
        V_string data;

        reader.start_overload();
        reader.required(data);    // data
        if(reader.end_overload())
          ASTERIA_BINDING_RETURN_MOVE(self,
                    std_checksum_SHA512_update, ::std::ref(hasher), data);
      }
      ASTERIA_BINDING_END);

    result.insert_or_assign(sref("finish"),
      ASTERIA_BINDING_BEGIN("std.checksum.SHA512::finish", self, global, reader) {
        self.push_modifier_object_key(s_uuid);
        auto& hasher = self.dereference_mutable().open_opaque();
        self.pop_modifier();

        reader.start_overload();
        if(reader.end_overload())
          ASTERIA_BINDING_RETURN_MOVE(self,
                    std_checksum_SHA512_finish, ::std::ref(hasher));
      }
      ASTERIA_BINDING_END);

    result.insert_or_assign(sref("clear"),
      ASTERIA_BINDING_BEGIN("std.checksum.SHA512::clear", self, global, reader) {
        self.push_modifier_object_key(s_uuid);
        auto& hasher = self.dereference_mutable().open_opaque();
        self.pop_modifier();

        reader.start_overload();
        if(reader.end_overload())
          ASTERIA_BINDING_RETURN_MOVE(self,
                    std_checksum_SHA512_clear, ::std::ref(hasher));
      }
      ASTERIA_BINDING_END);
  }

template<typename HasherT>
inline rcptr<HasherT>
do_cast_hasher(V_opaque& h)
  {
    auto hptr = h.open_opt<HasherT>();
    if(!hptr) {
      ASTERIA_THROW_RUNTIME_ERROR(
          "invalid hasher type (invalid dynamic_cast to `$1` from `$2`)",
          typeid(HasherT).name(), h.type().name());
    }
    return hptr;
  }

template<typename HasherT>
decltype(::std::declval<HasherT&>().finish())
do_hash_bytes(const V_string& data)
  {
    HasherT h;
    h.update(data.data(), data.size());
    return h.finish();
  }

template<typename HasherT>
decltype(::std::declval<HasherT&>().finish())
do_hash_file(const V_string& path)
  {
    // Open the file for reading.
    ::rocket::unique_posix_fd fd(::open(path.safe_c_str(), O_RDONLY), ::close);
    if(!fd)
      ASTERIA_THROW_RUNTIME_ERROR(
          "could not open file '$2'\n"
          "[`open()` failed: $1]",
          format_errno(errno), path);

    // Get the file mode and preferred I/O block size.
    struct ::stat stb;
    if(::fstat(fd, &stb) != 0)
      ASTERIA_THROW_RUNTIME_ERROR(
          "could not get information about source file '$2'\n"
          "[`fstat()` failed: $1]",
          format_errno(errno), path);

    // Allocate the I/O buffer.
    size_t nbuf = static_cast<size_t>(stb.st_blksize | 0x1000);
    auto pbuf = ::rocket::make_unique_handle(
                      static_cast<char*>(::operator new(nbuf)),
                      static_cast<void (*)(void*)>(::operator delete));

    // Read bytes from the file and hash them.
    HasherT h;

    ::ssize_t nread;
    while((nread = ::read(fd, pbuf, nbuf)) > 0)
      h.update(pbuf, static_cast<size_t>(nread));

    if(nread < 0)
      ASTERIA_THROW_RUNTIME_ERROR(
          "error reading file '$2'\n"
          "[`read()` failed: $1]",
          format_errno(errno), path);

    return h.finish();
  }

}  // namespace

V_opaque
std_checksum_CRC32_private()
  {
    return ::rocket::make_refcnt<CRC32_Hasher>();
  }

void
std_checksum_CRC32_update(V_opaque& h, V_string data)
  {
    return do_cast_hasher<CRC32_Hasher>(h)->update(data.data(), data.size());
  }

V_integer
std_checksum_CRC32_finish(V_opaque& h)
  {
    return do_cast_hasher<CRC32_Hasher>(h)->finish();
  }

void
std_checksum_CRC32_clear(V_opaque& h)
  {
    return do_cast_hasher<CRC32_Hasher>(h)->clear();
  }

V_object
std_checksum_CRC32()
  {
    V_object result;
    do_construct_CRC32(result);
    return result;
  }

V_integer
std_checksum_crc32(V_string data)
  {
    return do_hash_bytes<CRC32_Hasher>(data);
  }

V_integer
std_checksum_crc32_file(V_string path)
  {
    return do_hash_file<CRC32_Hasher>(path);
  }

V_opaque
std_checksum_FNV1a32_private()
  {
    return ::rocket::make_refcnt<FNV1a32_Hasher>();
  }

void
std_checksum_FNV1a32_update(V_opaque& h, V_string data)
  {
    return do_cast_hasher<FNV1a32_Hasher>(h)->update(data.data(), data.size());
  }

V_integer
std_checksum_FNV1a32_finish(V_opaque& h)
  {
    return do_cast_hasher<FNV1a32_Hasher>(h)->finish();
  }

void
std_checksum_FNV1a32_clear(V_opaque& h)
  {
    return do_cast_hasher<FNV1a32_Hasher>(h)->clear();
  }

V_object
std_checksum_FNV1a32()
  {
    V_object result;
    do_construct_FNV1a32(result);
    return result;
  }

V_integer
std_checksum_fnv1a32(V_string data)
  {
    return do_hash_bytes<FNV1a32_Hasher>(data);
  }

V_integer
std_checksum_fnv1a32_file(V_string path)
  {
    return do_hash_file<FNV1a32_Hasher>(path);
  }

V_opaque
std_checksum_MD5_private()
  {
    return ::rocket::make_refcnt<MD5_Hasher>();
  }

void
std_checksum_MD5_update(V_opaque& h, V_string data)
  {
    return do_cast_hasher<MD5_Hasher>(h)->update(data.data(), data.size());
  }

V_string
std_checksum_MD5_finish(V_opaque& h)
  {
    return do_cast_hasher<MD5_Hasher>(h)->finish();
  }

void
std_checksum_MD5_clear(V_opaque& h)
  {
    return do_cast_hasher<MD5_Hasher>(h)->clear();
  }

V_object
std_checksum_MD5()
  {
    V_object result;
    do_construct_MD5(result);
    return result;
  }

V_string
std_checksum_md5(V_string data)
  {
    return do_hash_bytes<MD5_Hasher>(data);
  }

V_string
std_checksum_md5_file(V_string path)
  {
    return do_hash_file<MD5_Hasher>(path);
  }

V_opaque
std_checksum_SHA1_private()
  {
    return ::rocket::make_refcnt<SHA1_Hasher>();
  }

void
std_checksum_SHA1_update(V_opaque& h, V_string data)
  {
    return do_cast_hasher<SHA1_Hasher>(h)->update(data.data(), data.size());
  }

V_string
std_checksum_SHA1_finish(V_opaque& h)
  {
    return do_cast_hasher<SHA1_Hasher>(h)->finish();
  }

void
std_checksum_SHA1_clear(V_opaque& h)
  {
    return do_cast_hasher<SHA1_Hasher>(h)->clear();
  }

V_object
std_checksum_SHA1()
  {
    V_object result;
    do_construct_SHA1(result);
    return result;
  }

V_string
std_checksum_sha1(V_string data)
  {
    return do_hash_bytes<SHA1_Hasher>(data);
  }

V_string
std_checksum_sha1_file(V_string path)
  {
    return do_hash_file<SHA1_Hasher>(path);
  }

V_opaque
std_checksum_SHA224_private()
  {
    return ::rocket::make_refcnt<SHA224_Hasher>();
  }

void
std_checksum_SHA224_update(V_opaque& h, V_string data)
  {
    return do_cast_hasher<SHA224_Hasher>(h)->update(data.data(), data.size());
  }

V_string
std_checksum_SHA224_finish(V_opaque& h)
  {
    return do_cast_hasher<SHA224_Hasher>(h)->finish();
  }

void
std_checksum_SHA224_clear(V_opaque& h)
  {
    return do_cast_hasher<SHA224_Hasher>(h)->clear();
  }

V_object
std_checksum_SHA224()
  {
    V_object result;
    do_construct_SHA224(result);
    return result;
  }

V_string
std_checksum_sha224(V_string data)
  {
    return do_hash_bytes<SHA224_Hasher>(data);
  }

V_string
std_checksum_sha224_file(V_string path)
  {
    return do_hash_file<SHA224_Hasher>(path);
  }

V_opaque
std_checksum_SHA256_private()
  {
    return ::rocket::make_refcnt<SHA256_Hasher>();
  }

void
std_checksum_SHA256_update(V_opaque& h, V_string data)
  {
    return do_cast_hasher<SHA256_Hasher>(h)->update(data.data(), data.size());
  }

V_string
std_checksum_SHA256_finish(V_opaque& h)
  {
    return do_cast_hasher<SHA256_Hasher>(h)->finish();
  }

void
std_checksum_SHA256_clear(V_opaque& h)
  {
    return do_cast_hasher<SHA256_Hasher>(h)->clear();
  }

V_object
std_checksum_SHA256()
  {
    V_object result;
    do_construct_SHA256(result);
    return result;
  }

V_string
std_checksum_sha256(V_string data)
  {
    return do_hash_bytes<SHA256_Hasher>(data);
  }

V_string
std_checksum_sha256_file(V_string path)
  {
    return do_hash_file<SHA256_Hasher>(path);
  }

V_opaque
std_checksum_SHA384_private()
  {
    return ::rocket::make_refcnt<SHA384_Hasher>();
  }

void
std_checksum_SHA384_update(V_opaque& h, V_string data)
  {
    return do_cast_hasher<SHA384_Hasher>(h)->update(data.data(), data.size());
  }

V_string
std_checksum_SHA384_finish(V_opaque& h)
  {
    return do_cast_hasher<SHA384_Hasher>(h)->finish();
  }

void
std_checksum_SHA384_clear(V_opaque& h)
  {
    return do_cast_hasher<SHA384_Hasher>(h)->clear();
  }

V_object
std_checksum_SHA384()
  {
    V_object result;
    do_construct_SHA384(result);
    return result;
  }

V_string
std_checksum_sha384(V_string data)
  {
    return do_hash_bytes<SHA384_Hasher>(data);
  }

V_string
std_checksum_sha384_file(V_string path)
  {
    return do_hash_file<SHA384_Hasher>(path);
  }

V_opaque
std_checksum_SHA512_private()
  {
    return ::rocket::make_refcnt<SHA512_Hasher>();
  }

void
std_checksum_SHA512_update(V_opaque& h, V_string data)
  {
    return do_cast_hasher<SHA512_Hasher>(h)->update(data.data(), data.size());
  }

V_string
std_checksum_SHA512_finish(V_opaque& h)
  {
    return do_cast_hasher<SHA512_Hasher>(h)->finish();
  }

void
std_checksum_SHA512_clear(V_opaque& h)
  {
    return do_cast_hasher<SHA512_Hasher>(h)->clear();
  }

V_object
std_checksum_SHA512()
  {
    V_object result;
    do_construct_SHA512(result);
    return result;
  }

V_string
std_checksum_sha512(V_string data)
  {
    return do_hash_bytes<SHA512_Hasher>(data);
  }

V_string
std_checksum_sha512_file(V_string path)
  {
    return do_hash_file<SHA512_Hasher>(path);
  }

void
create_bindings_checksum(V_object& result, API_Version /*version*/)
  {
    result.insert_or_assign(sref("CRC32"),
      ASTERIA_BINDING_BEGIN("std.checksum.CRC32", self, global, reader) {
        reader.start_overload();
        if(reader.end_overload())
          ASTERIA_BINDING_RETURN_MOVE(self,
                    std_checksum_CRC32);
      }
      ASTERIA_BINDING_END);

    result.insert_or_assign(sref("crc32"),
      ASTERIA_BINDING_BEGIN("std.checksum.crc32", self, global, reader) {
        V_string data;

        reader.start_overload();
        reader.required(data);    // data
        if(reader.end_overload())
          ASTERIA_BINDING_RETURN_MOVE(self,
                    std_checksum_crc32, data);
      }
      ASTERIA_BINDING_END);

    result.insert_or_assign(sref("crc32_file"),
      ASTERIA_BINDING_BEGIN("std.checksum.crc32_file", self, global, reader) {
        V_string path;

        reader.start_overload();
        reader.required(path);    // path
        if(reader.end_overload())
          ASTERIA_BINDING_RETURN_MOVE(self,
                    std_checksum_crc32_file, path);
      }
      ASTERIA_BINDING_END);

    result.insert_or_assign(sref("FNV1a32"),
      ASTERIA_BINDING_BEGIN("std.checksum.FNV1a32", self, global, reader) {
        reader.start_overload();
        if(reader.end_overload())
          ASTERIA_BINDING_RETURN_MOVE(self,
                    std_checksum_FNV1a32);
      }
      ASTERIA_BINDING_END);

    result.insert_or_assign(sref("fnv1a32"),
      ASTERIA_BINDING_BEGIN("std.checksum.fnv1a32", self, global, reader) {
        V_string data;

        reader.start_overload();
        reader.required(data);    // data
        if(reader.end_overload())
          ASTERIA_BINDING_RETURN_MOVE(self,
                    std_checksum_fnv1a32, data);
      }
      ASTERIA_BINDING_END);

    result.insert_or_assign(sref("fnv1a32_file"),
      ASTERIA_BINDING_BEGIN("std.checksum.fnv1a32_file", self, global, reader) {
        V_string path;

        reader.start_overload();
        reader.required(path);    // path
        if(reader.end_overload())
          ASTERIA_BINDING_RETURN_MOVE(self,
                    std_checksum_fnv1a32_file, path);
      }
      ASTERIA_BINDING_END);

    result.insert_or_assign(sref("MD5"),
      ASTERIA_BINDING_BEGIN("std.checksum.MD5", self, global, reader) {
        reader.start_overload();
        if(reader.end_overload())
          ASTERIA_BINDING_RETURN_MOVE(self,
                    std_checksum_MD5);
      }
      ASTERIA_BINDING_END);

    result.insert_or_assign(sref("md5"),
      ASTERIA_BINDING_BEGIN("std.checksum.md5", self, global, reader) {
        V_string data;

        reader.start_overload();
        reader.required(data);    // data
        if(reader.end_overload())
          ASTERIA_BINDING_RETURN_MOVE(self,
                    std_checksum_md5, data);
      }
      ASTERIA_BINDING_END);

    result.insert_or_assign(sref("md5_file"),
      ASTERIA_BINDING_BEGIN("std.checksum.md5_file", self, global, reader) {
        V_string path;

        reader.start_overload();
        reader.required(path);    // path
        if(reader.end_overload())
          ASTERIA_BINDING_RETURN_MOVE(self,
                    std_checksum_md5_file, path);
      }
      ASTERIA_BINDING_END);

    result.insert_or_assign(sref("SHA1"),
      ASTERIA_BINDING_BEGIN("std.checksum.SHA1", self, global, reader) {
        reader.start_overload();
        if(reader.end_overload())
          ASTERIA_BINDING_RETURN_MOVE(self,
                    std_checksum_SHA1);
      }
      ASTERIA_BINDING_END);

    result.insert_or_assign(sref("sha1"),
      ASTERIA_BINDING_BEGIN("std.checksum.sha1", self, global, reader) {
        V_string data;

        reader.start_overload();
        reader.required(data);    // data
        if(reader.end_overload())
          ASTERIA_BINDING_RETURN_MOVE(self,
                    std_checksum_sha1, data);
      }
      ASTERIA_BINDING_END);

    result.insert_or_assign(sref("sha1_file"),
      ASTERIA_BINDING_BEGIN("std.checksum.sha1_file", self, global, reader) {
        V_string path;

        reader.start_overload();
        reader.required(path);    // path
        if(reader.end_overload())
          ASTERIA_BINDING_RETURN_MOVE(self,
                    std_checksum_sha1_file, path);
      }
      ASTERIA_BINDING_END);

    result.insert_or_assign(sref("SHA224"),
      ASTERIA_BINDING_BEGIN("std.checksum.SHA224", self, global, reader) {
        reader.start_overload();
        if(reader.end_overload())
          ASTERIA_BINDING_RETURN_MOVE(self,
                    std_checksum_SHA224);
      }
      ASTERIA_BINDING_END);

    result.insert_or_assign(sref("sha224"),
      ASTERIA_BINDING_BEGIN("std.checksum.sha224", self, global, reader) {
        V_string data;

        reader.start_overload();
        reader.required(data);    // data
        if(reader.end_overload())
          ASTERIA_BINDING_RETURN_MOVE(self,
                    std_checksum_sha224, data);
      }
      ASTERIA_BINDING_END);

    result.insert_or_assign(sref("sha224_file"),
      ASTERIA_BINDING_BEGIN("std.checksum.sha224_file", self, global, reader) {
        V_string path;

        reader.start_overload();
        reader.required(path);    // path
        if(reader.end_overload())
          ASTERIA_BINDING_RETURN_MOVE(self,
                    std_checksum_sha224_file, path);
      }
      ASTERIA_BINDING_END);

    result.insert_or_assign(sref("SHA256"),
      ASTERIA_BINDING_BEGIN("std.checksum.SHA256", self, global, reader) {
        reader.start_overload();
        if(reader.end_overload())
          ASTERIA_BINDING_RETURN_MOVE(self,
                    std_checksum_SHA256);
      }
      ASTERIA_BINDING_END);

    result.insert_or_assign(sref("sha256"),
      ASTERIA_BINDING_BEGIN("std.checksum.sha256", self, global, reader) {
        V_string data;

        reader.start_overload();
        reader.required(data);    // data
        if(reader.end_overload())
          ASTERIA_BINDING_RETURN_MOVE(self,
                    std_checksum_sha256, data);
      }
      ASTERIA_BINDING_END);

    result.insert_or_assign(sref("sha256_file"),
      ASTERIA_BINDING_BEGIN("std.checksum.sha256_file", self, global, reader) {
        V_string path;

        reader.start_overload();
        reader.required(path);    // path
        if(reader.end_overload())
          ASTERIA_BINDING_RETURN_MOVE(self,
                    std_checksum_sha256_file, path);
      }
      ASTERIA_BINDING_END);

    result.insert_or_assign(sref("SHA384"),
      ASTERIA_BINDING_BEGIN("std.checksum.SHA384", self, global, reader) {
        reader.start_overload();
        if(reader.end_overload())
          ASTERIA_BINDING_RETURN_MOVE(self,
                    std_checksum_SHA384);
      }
      ASTERIA_BINDING_END);

    result.insert_or_assign(sref("sha384"),
      ASTERIA_BINDING_BEGIN("std.checksum.sha384", self, global, reader) {
        V_string data;

        reader.start_overload();
        reader.required(data);    // data
        if(reader.end_overload())
          ASTERIA_BINDING_RETURN_MOVE(self,
                    std_checksum_sha384, data);
      }
      ASTERIA_BINDING_END);

    result.insert_or_assign(sref("sha384_file"),
      ASTERIA_BINDING_BEGIN("std.checksum.sha384_file", self, global, reader) {
        V_string path;

        reader.start_overload();
        reader.required(path);    // path
        if(reader.end_overload())
          ASTERIA_BINDING_RETURN_MOVE(self,
                    std_checksum_sha384_file, path);
      }
      ASTERIA_BINDING_END);

    result.insert_or_assign(sref("SHA512"),
      ASTERIA_BINDING_BEGIN("std.checksum.SHA512", self, global, reader) {
        reader.start_overload();
        if(reader.end_overload())
          ASTERIA_BINDING_RETURN_MOVE(self,
                    std_checksum_SHA512);
      }
      ASTERIA_BINDING_END);

    result.insert_or_assign(sref("sha512"),
      ASTERIA_BINDING_BEGIN("std.checksum.sha512", self, global, reader) {
        V_string data;

        reader.start_overload();
        reader.required(data);    // data
        if(reader.end_overload())
          ASTERIA_BINDING_RETURN_MOVE(self,
                    std_checksum_sha512, data);
      }
      ASTERIA_BINDING_END);

    result.insert_or_assign(sref("sha512_file"),
      ASTERIA_BINDING_BEGIN("std.checksum.sha512_file", self, global, reader) {
        V_string path;

        reader.start_overload();
        reader.required(path);    // path
        if(reader.end_overload())
          ASTERIA_BINDING_RETURN_MOVE(self,
                    std_checksum_sha512_file, path);
      }
      ASTERIA_BINDING_END);
  }

}  // namespace asteria
