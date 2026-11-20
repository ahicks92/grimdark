#pragma once
// Private helpers shared by the gameapi_*.cpp translation units: export loading, the game's container
// layouts (mem::vector, mem::map, std::basic_string<unsigned short>, GameTextLine), SEH-guarded reads and
// calls, and the vtable-slot finder for the virtual text builders. Game thread only.
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include "gd_names.h"
#include "log.h"
#include "msvc_string.h"
#include "textcap.h"

namespace gd::gameapi::detail {

template <class F> F fn(const char* dll, const char* name) {
  HMODULE m = GetModuleHandleA(dll);
  return m ? (F)GetProcAddress(m, name) : nullptr;
}
// Resolve `api.field` from the gd_names.h identifier ID; logs a missing export once.
#define GAPI_LOAD(api, field, ID) \
  (api).field = ::gd::gameapi::detail::fn<decltype((api).field)>(ID##_DLL, ID); \
  if (!(api).field) ::gd::log::writef("gameapi: export {} not found", #ID)

// mem::vector<T>: std-like {begin, end, cap} (read from Engine.dll; AGENTS.md).
struct MemVec { void* begin = nullptr; void* end = nullptr; void* cap = nullptr; };

// ---- SEH-guarded memory access (no C++ objects with destructors inside these frames) ----
inline bool read_mem(const void* src, void* dst, size_t n) {
  __try { memcpy(dst, src, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
template <class T> bool rd(const void* base, size_t off, T& out) { return base && read_mem((const char*)base + off, &out, sizeof out); }
template <class T> T rd_or(const void* base, size_t off, T def) { T v; return rd(base, off, v) ? v : def; }
inline void* rdp(const void* base, size_t off) { return rd_or<void*>(base, off, nullptr); }

// Run a callable under an SEH guard: a fault inside is logged and reported as false instead of killing
// the game. The C++ frame of the callable is not unwound on a fault (objects in it leak), which is the
// accepted price for not crashing. The SEH frame itself holds no C++ objects.
typedef void (*Thunk)(void*);
inline bool seh_invoke(Thunk t, void* ctx, const char* what) {
  __try { t(ctx); return true; } __except (EXCEPTION_EXECUTE_HANDLER) {
    ::gd::log::writef("gameapi: {} faulted (exception {:#x})", what, (unsigned)GetExceptionCode());
    return false;
  }
}
template <class F> void thunk_of(void* p) { (*(F*)p)(); }
template <class F> bool guarded(const char* what, F&& f) { return seh_invoke(&thunk_of<std::remove_reference_t<F>>, (void*)&f, what); }

// The elements of a mem::vector<T> (copied out, bounded).
template <class T> std::vector<T> vec_items(const MemVec* v, size_t max_items = 65536) {
  std::vector<T> out;
  MemVec mv;
  if (!v || !read_mem(v, &mv, sizeof mv) || !mv.begin || !mv.end || mv.end < mv.begin) return out;
  size_t n = ((char*)mv.end - (char*)mv.begin) / sizeof(T);
  if (n > max_items) n = max_items;
  out.resize(n);
  if (n && !read_mem(mv.begin, out.data(), n * sizeof(T))) out.clear();
  return out;
}

// A std::basic_string<unsigned short> the game owns (by pointer) -> UTF-8 with the colour codes stripped.
inline std::string u16_text(const MsvcStringW* s) {
  MsvcStringW c;
  if (!s || !read_mem(s, &c, sizeof c) || c.size > 65536) return {};
  if (c.capacity < 8) return textcap::speakable(std::u16string_view(c.u.buf, c.size));
  std::u16string buf(c.size, u'\0');
  if (!c.u.ptr || !read_mem(c.u.ptr, buf.data(), c.size * 2)) return {};
  return textcap::speakable(buf);
}
inline std::string a_text(const MsvcStringA* s) {
  MsvcStringA c;
  if (!s || !read_mem(s, &c, sizeof c) || c.size > 65536) return {};
  if (c.capacity < 16) return std::string(c.u.buf, c.size);
  std::string buf(c.size, '\0');
  if (!c.u.ptr || !read_mem(c.u.ptr, buf.data(), c.size)) return {};
  return buf;
}
// An empty, SSO-state string for the game to fill (out-params and hidden return pointers).
inline void init_u16(MsvcStringW& s) { memset(&s, 0, sizeof s); s.capacity = 7; }
inline void init_a(MsvcStringA& s) { memset(&s, 0, sizeof s); s.capacity = 15; }
// Take the text out of a string the game filled and release its heap buffer (the game's std::allocator is
// the UCRT heap, which is the process heap; src/world.cpp's GetTypeTag readout set the precedent).
inline std::string take_u16(MsvcStringW& s) {
  std::string t = u16_text(&s);
  if (s.capacity >= 8 && s.u.ptr) free(s.u.ptr);
  init_u16(s);
  return t;
}
inline std::string take_a(MsvcStringA& s) {
  std::string t = a_text(&s);
  if (s.capacity >= 16 && s.u.ptr) free(s.u.ptr);
  init_a(s);
  return t;
}

// mem::vector<GameTextLine> (stride 0x40: +0 GameTextClass, +8 u16 string; docs/ingame-ui-survey.md). The
// caller provides the buffer (capacity `cap` lines) so the builder never allocates through the game's
// allocator; the line strings are freed here. Returns the lines' text, colour codes stripped; empty lines kept
// out. Logs when the buffer was too small (the builder would then have grown it with the game's allocator and
// freed OUR buffer -- which is why the caps are generous).
struct TextLine { int cls; std::string text; };
constexpr size_t kTextLineStride = 0x40;
class TextLineBuffer {
 public:
  explicit TextLineBuffer(size_t cap = 512) : raw_(cap * kTextLineStride) { v_.begin = raw_.data(); v_.end = raw_.data(); v_.cap = raw_.data() + raw_.size(); }
  MemVec* vec() { return &v_; }
  std::vector<TextLine> take(const char* what) {
    std::vector<TextLine> out;
    size_t n = v_.end && v_.begin ? ((char*)v_.end - (char*)v_.begin) / kTextLineStride : 0;
    if (v_.end == v_.cap || v_.begin != raw_.data()) { ::gd::log::writef("gameapi: {} overflowed the text-line buffer (begin moved={})", what, v_.begin != raw_.data()); if (v_.begin != raw_.data()) return out; }
    for (size_t i = 0; i < n; ++i) {
      char* line = raw_.data() + i * kTextLineStride;
      int cls = 0; memcpy(&cls, line, sizeof cls);
      MsvcStringW* s = (MsvcStringW*)(line + 8);
      std::string t = take_u16(*s);
      if (!t.empty()) out.push_back({cls, t});
    }
    v_.end = v_.begin;
    return out;
  }
 private:
  std::vector<char> raw_;
  MemVec v_;
};

// A caller-owned buffer for a mem::vector<T> the game APPENDS to.
template <class T> class VecBuffer {
 public:
  explicit VecBuffer(size_t cap) : buf_(cap) { v_.begin = buf_.data(); v_.end = buf_.data(); v_.cap = buf_.data() + buf_.size(); }
  MemVec* vec() { return &v_; }
  std::vector<T> take(const char* what) {
    std::vector<T> out;
    if (v_.begin != buf_.data()) { ::gd::log::writef("gameapi: {} reallocated our vector buffer", what); return out; }
    if (v_.end == v_.cap) ::gd::log::writef("gameapi: {} filled the vector buffer exactly (raise the cap)", what);
    size_t n = ((char*)v_.end - (char*)v_.begin) / sizeof(T);
    out.assign(buf_.begin(), buf_.begin() + n);
    v_.end = v_.begin;
    return out;
  }
 private:
  std::vector<T> buf_;
  MemVec v_;
};

// mem::map<K, V>: an MSVC std::map (red-black tree): {head, size}; node = {left, parent, right, colour,
// isnil, key @+0x20, value @+0x28} (verified on the market map, docs/ingame-ui-survey.md). In-order walk,
// bounded, SEH-guarded, returning the node pointers.
inline std::vector<void*> map_nodes(const void* map, size_t max_nodes = 4096) {
  std::vector<void*> out;
  void* head = rdp(map, 0);
  if (!head) return out;
  // leftmost
  void* n = rdp(head, 0);   // head->left = leftmost
  size_t guard = 0;
  while (n && n != head && rd_or<uint8_t>(n, 0x19, 1) == 0 && guard++ < max_nodes) {
    out.push_back(n);
    // successor
    void* r = rdp(n, 0x10);
    if (r && rd_or<uint8_t>(r, 0x19, 1) == 0) {
      n = r;
      void* l;
      while ((l = rdp(n, 0)) && rd_or<uint8_t>(l, 0x19, 1) == 0) n = l;
    } else {
      void* p = rdp(n, 8);
      while (p && rd_or<uint8_t>(p, 0x19, 1) == 0 && rdp(p, 0x10) == n) { n = p; p = rdp(n, 8); }
      n = p;
    }
  }
  return out;
}

// Find a virtual function's slot in an exported vftable (the export is the class's base implementation; the
// object's own vtable may override it, so the slot is looked up once and dispatched through the object).
// Identical base implementations (`return 0`) are COMDAT-folded by the linker, so one address can sit in
// several slots: that is reported as ambiguous (-1, logged) rather than guessed -- resolve such a function
// through a subclass's own exported override instead.
inline int vslot(void** vftable, const void* base_fn, int max_slots = 256) {
  if (!vftable || !base_fn) return -1;
  int found = -1, count = 0;
  for (int i = 0; i < max_slots; ++i) { void* e; if (!read_mem(vftable + i, &e, sizeof e)) break; if (e == base_fn) { if (found < 0) found = i; ++count; } }
  if (count > 1) { ::gd::log::writef("gameapi: vtable slot for {} is ambiguous ({} slots, first {})", base_fn, count, found); return -1; }
  return found;
}
inline void* vfn(const void* obj, int slot) {
  void** vt = (void**)rdp(obj, 0);
  return vt && slot >= 0 ? rdp(vt, (size_t)slot * sizeof(void*)) : nullptr;
}

}  // namespace gd::gameapi::detail
