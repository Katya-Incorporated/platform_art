/*
 * Copyright (C) 2008 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "sun_misc_Unsafe.h"

#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <atomic>

#include "nativehelper/jni_macros.h"

#include "base/quasi_atomic.h"
#include "common_throws.h"
#include "gc/accounting/card_table-inl.h"
#include "jni/jni_internal.h"
#include "mirror/array.h"
#include "mirror/class-inl.h"
#include "mirror/object-inl.h"
#include "art_field-inl.h"
#include "native_util.h"
#include "scoped_fast_native_object_access-inl.h"
#include "well_known_classes-inl.h"

namespace art HIDDEN {

static jboolean Unsafe_compareAndSwapInt(JNIEnv* env, jobject, jobject javaObj, jlong offset,
                                         jint expectedValue, jint newValue) {
  ScopedFastNativeObjectAccess soa(env);
  ObjPtr<mirror::Object> obj = soa.Decode<mirror::Object>(javaObj);
  // JNI must use non transactional mode.
  bool success = obj->CasField32<false>(MemberOffset(offset),
                                        expectedValue,
                                        newValue,
                                        CASMode::kStrong,
                                        std::memory_order_seq_cst);
  return success ? JNI_TRUE : JNI_FALSE;
}

static jboolean Unsafe_compareAndSwapLong(JNIEnv* env, jobject, jobject javaObj, jlong offset,
                                          jlong expectedValue, jlong newValue) {
  ScopedFastNativeObjectAccess soa(env);
  ObjPtr<mirror::Object> obj = soa.Decode<mirror::Object>(javaObj);
  // JNI must use non transactional mode.
  bool success = obj->CasFieldStrongSequentiallyConsistent64<false>(MemberOffset(offset),
                                                                    expectedValue,
                                                                    newValue);
  return success ? JNI_TRUE : JNI_FALSE;
}

static jboolean Unsafe_compareAndSwapObject(JNIEnv* env, jobject, jobject javaObj, jlong offset,
                                            jobject javaExpectedValue, jobject javaNewValue) {
  ScopedFastNativeObjectAccess soa(env);
  ObjPtr<mirror::Object> obj = soa.Decode<mirror::Object>(javaObj);
  ObjPtr<mirror::Object> expectedValue = soa.Decode<mirror::Object>(javaExpectedValue);
  ObjPtr<mirror::Object> newValue = soa.Decode<mirror::Object>(javaNewValue);
  // JNI must use non transactional mode.
  if (gUseReadBarrier) {
    // Need to make sure the reference stored in the field is a to-space one before attempting the
    // CAS or the CAS could fail incorrectly.
    // Note that the read barrier load does NOT need to be volatile.
    mirror::HeapReference<mirror::Object>* field_addr =
        reinterpret_cast<mirror::HeapReference<mirror::Object>*>(
            reinterpret_cast<uint8_t*>(obj.Ptr()) + static_cast<size_t>(offset));
    ReadBarrier::Barrier<mirror::Object, /* kIsVolatile= */ false, kWithReadBarrier,
        /* kAlwaysUpdateField= */ true>(
        obj.Ptr(),
        MemberOffset(offset),
        field_addr);
  }
  bool success = obj->CasFieldObject<false>(MemberOffset(offset),
                                            expectedValue,
                                            newValue,
                                            CASMode::kStrong,
                                            std::memory_order_seq_cst);
  return success ? JNI_TRUE : JNI_FALSE;
}

static jint Unsafe_getInt(JNIEnv* env, jobject, jobject javaObj, jlong offset) {
  ScopedFastNativeObjectAccess soa(env);
  ObjPtr<mirror::Object> obj = soa.Decode<mirror::Object>(javaObj);
  return obj->GetField32(MemberOffset(offset));
}

static jint Unsafe_getIntVolatile(JNIEnv* env, jobject, jobject javaObj, jlong offset) {
  ScopedFastNativeObjectAccess soa(env);
  ObjPtr<mirror::Object> obj = soa.Decode<mirror::Object>(javaObj);
  return obj->GetField32Volatile(MemberOffset(offset));
}

static void Unsafe_putInt(JNIEnv* env, jobject, jobject javaObj, jlong offset, jint newValue) {
  ScopedFastNativeObjectAccess soa(env);
  ObjPtr<mirror::Object> obj = soa.Decode<mirror::Object>(javaObj);
  // JNI must use non transactional mode.
  obj->SetField32<false>(MemberOffset(offset), newValue);
}

static void Unsafe_putIntVolatile(JNIEnv* env, jobject, jobject javaObj, jlong offset,
                                  jint newValue) {
  ScopedFastNativeObjectAccess soa(env);
  ObjPtr<mirror::Object> obj = soa.Decode<mirror::Object>(javaObj);
  // JNI must use non transactional mode.
  obj->SetField32Volatile<false>(MemberOffset(offset), newValue);
}

static void Unsafe_putOrderedInt(JNIEnv* env, jobject, jobject javaObj, jlong offset,
                                 jint newValue) {
  ScopedFastNativeObjectAccess soa(env);
  ObjPtr<mirror::Object> obj = soa.Decode<mirror::Object>(javaObj);
  // TODO: A release store is likely to be faster on future processors.
  std::atomic_thread_fence(std::memory_order_release);
  // JNI must use non transactional mode.
  obj->SetField32<false>(MemberOffset(offset), newValue);
}

static jlong Unsafe_getLong(JNIEnv* env, jobject, jobject javaObj, jlong offset) {
  ScopedFastNativeObjectAccess soa(env);
  ObjPtr<mirror::Object> obj = soa.Decode<mirror::Object>(javaObj);
  return obj->GetField64(MemberOffset(offset));
}

static jlong Unsafe_getLongVolatile(JNIEnv* env, jobject, jobject javaObj, jlong offset) {
  ScopedFastNativeObjectAccess soa(env);
  ObjPtr<mirror::Object> obj = soa.Decode<mirror::Object>(javaObj);
  return obj->GetField64Volatile(MemberOffset(offset));
}

static void Unsafe_putLong(JNIEnv* env, jobject, jobject javaObj, jlong offset, jlong newValue) {
  ScopedFastNativeObjectAccess soa(env);
  ObjPtr<mirror::Object> obj = soa.Decode<mirror::Object>(javaObj);
  // JNI must use non transactional mode.
  obj->SetField64<false>(MemberOffset(offset), newValue);
}

static void Unsafe_putLongVolatile(JNIEnv* env, jobject, jobject javaObj, jlong offset,
                                   jlong newValue) {
  ScopedFastNativeObjectAccess soa(env);
  ObjPtr<mirror::Object> obj = soa.Decode<mirror::Object>(javaObj);
  // JNI must use non transactional mode.
  obj->SetField64Volatile<false>(MemberOffset(offset), newValue);
}

static void Unsafe_putOrderedLong(JNIEnv* env, jobject, jobject javaObj, jlong offset,
                                  jlong newValue) {
  ScopedFastNativeObjectAccess soa(env);
  ObjPtr<mirror::Object> obj = soa.Decode<mirror::Object>(javaObj);
  std::atomic_thread_fence(std::memory_order_release);
  // JNI must use non transactional mode.
  obj->SetField64<false>(MemberOffset(offset), newValue);
}

static jobject Unsafe_getObjectVolatile(JNIEnv* env, jobject, jobject javaObj, jlong offset) {
  ScopedFastNativeObjectAccess soa(env);
  ObjPtr<mirror::Object> obj = soa.Decode<mirror::Object>(javaObj);
  ObjPtr<mirror::Object> value = obj->GetFieldObjectVolatile<mirror::Object>(MemberOffset(offset));
  return soa.AddLocalReference<jobject>(value);
}

static jobject Unsafe_getObject(JNIEnv* env, jobject, jobject javaObj, jlong offset) {
  ScopedFastNativeObjectAccess soa(env);
  ObjPtr<mirror::Object> obj = soa.Decode<mirror::Object>(javaObj);
  ObjPtr<mirror::Object> value = obj->GetFieldObject<mirror::Object>(MemberOffset(offset));
  return soa.AddLocalReference<jobject>(value);
}

static void Unsafe_putObject(JNIEnv* env, jobject, jobject javaObj, jlong offset,
                             jobject javaNewValue) {
  ScopedFastNativeObjectAccess soa(env);
  ObjPtr<mirror::Object> obj = soa.Decode<mirror::Object>(javaObj);
  ObjPtr<mirror::Object> newValue = soa.Decode<mirror::Object>(javaNewValue);
  // JNI must use non transactional mode.
  obj->SetFieldObject<false>(MemberOffset(offset), newValue);
}

static void Unsafe_putObjectVolatile(JNIEnv* env, jobject, jobject javaObj, jlong offset,
                                     jobject javaNewValue) {
  ScopedFastNativeObjectAccess soa(env);
  ObjPtr<mirror::Object> obj = soa.Decode<mirror::Object>(javaObj);
  ObjPtr<mirror::Object> newValue = soa.Decode<mirror::Object>(javaNewValue);
  // JNI must use non transactional mode.
  obj->SetFieldObjectVolatile<false>(MemberOffset(offset), newValue);
}

static void Unsafe_putOrderedObject(JNIEnv* env, jobject, jobject javaObj, jlong offset,
                                    jobject javaNewValue) {
  ScopedFastNativeObjectAccess soa(env);
  ObjPtr<mirror::Object> obj = soa.Decode<mirror::Object>(javaObj);
  ObjPtr<mirror::Object> newValue = soa.Decode<mirror::Object>(javaNewValue);
  std::atomic_thread_fence(std::memory_order_release);
  // JNI must use non transactional mode.
  obj->SetFieldObject<false>(MemberOffset(offset), newValue);
}

static jint Unsafe_getArrayBaseOffsetForComponentType(JNIEnv* env, jclass, jclass component_class) {
  ScopedFastNativeObjectAccess soa(env);
  ObjPtr<mirror::Class> component = soa.Decode<mirror::Class>(component_class);
  Primitive::Type primitive_type = component->GetPrimitiveType();
  return mirror::Array::DataOffset(Primitive::ComponentSize(primitive_type)).Int32Value();
}

static jint Unsafe_getArrayIndexScaleForComponentType(JNIEnv* env, jclass, jclass component_class) {
  ScopedFastNativeObjectAccess soa(env);
  ObjPtr<mirror::Class> component = soa.Decode<mirror::Class>(component_class);
  Primitive::Type primitive_type = component->GetPrimitiveType();
  return Primitive::ComponentSize(primitive_type);
}

static jint Unsafe_addressSize([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jobject ob) {
  return sizeof(void*);
}

static jint Unsafe_pageSize([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jobject ob) {
  return sysconf(_SC_PAGESIZE);
}

static jlong Unsafe_allocateMemory(JNIEnv* env, jobject, jlong bytes) {
  ScopedFastNativeObjectAccess soa(env);
  // bytes is nonnegative and fits into size_t
  if (bytes < 0 || bytes != static_cast<jlong>(static_cast<size_t>(bytes))) {
    ThrowIllegalAccessException("wrong number of bytes");
    return 0;
  }
  void* mem = malloc(bytes);
  if (mem == nullptr) {
    soa.Self()->ThrowOutOfMemoryError("native alloc");
    return 0;
  }
  return (uintptr_t) mem;
}

static void Unsafe_freeMemory([[maybe_unused]] JNIEnv* env, jobject, jlong address) {
  free(reinterpret_cast<void*>(static_cast<uintptr_t>(address)));
}

static void Unsafe_setMemory(
    [[maybe_unused]] JNIEnv* env, jobject, jlong address, jlong bytes, jbyte value) {
  memset(reinterpret_cast<void*>(static_cast<uintptr_t>(address)), value, bytes);
}

static jbyte Unsafe_getByteJ([[maybe_unused]] JNIEnv* env, jobject, jlong address) {
  return *reinterpret_cast<jbyte*>(address);
}

static void Unsafe_putByteJB([[maybe_unused]] JNIEnv* env, jobject, jlong address, jbyte value) {
  *reinterpret_cast<jbyte*>(address) = value;
}

static jshort Unsafe_getShortJ([[maybe_unused]] JNIEnv* env, jobject, jlong address) {
  return *reinterpret_cast<jshort*>(address);
}

static void Unsafe_putShortJS([[maybe_unused]] JNIEnv* env, jobject, jlong address, jshort value) {
  *reinterpret_cast<jshort*>(address) = value;
}

static jchar Unsafe_getCharJ([[maybe_unused]] JNIEnv* env, jobject, jlong address) {
  return *reinterpret_cast<jchar*>(address);
}

static void Unsafe_putCharJC([[maybe_unused]] JNIEnv* env, jobject, jlong address, jchar value) {
  *reinterpret_cast<jchar*>(address) = value;
}

static jint Unsafe_getIntJ([[maybe_unused]] JNIEnv* env, jobject, jlong address) {
  return *reinterpret_cast<jint*>(address);
}

static void Unsafe_putIntJI([[maybe_unused]] JNIEnv* env, jobject, jlong address, jint value) {
  *reinterpret_cast<jint*>(address) = value;
}

static jlong Unsafe_getLongJ([[maybe_unused]] JNIEnv* env, jobject, jlong address) {
  return *reinterpret_cast<jlong*>(address);
}

static void Unsafe_putLongJJ([[maybe_unused]] JNIEnv* env, jobject, jlong address, jlong value) {
  *reinterpret_cast<jlong*>(address) = value;
}

static jfloat Unsafe_getFloatJ([[maybe_unused]] JNIEnv* env, jobject, jlong address) {
  return *reinterpret_cast<jfloat*>(address);
}

static void Unsafe_putFloatJF([[maybe_unused]] JNIEnv* env, jobject, jlong address, jfloat value) {
  *reinterpret_cast<jfloat*>(address) = value;
}
static jdouble Unsafe_getDoubleJ([[maybe_unused]] JNIEnv* env, jobject, jlong address) {
  return *reinterpret_cast<jdouble*>(address);
}

static void Unsafe_putDoubleJD([[maybe_unused]] JNIEnv* env,
                               jobject,
                               jlong address,
                               jdouble value) {
  *reinterpret_cast<jdouble*>(address) = value;
}

static void Unsafe_copyMemory(
    JNIEnv* env, [[maybe_unused]] jobject unsafe, jlong src, jlong dst, jlong size) {
  if (size == 0) {
    return;
  }
  // size is nonnegative and fits into size_t
  if (size < 0 || size != static_cast<jlong>(static_cast<size_t>(size))) {
    ScopedFastNativeObjectAccess soa(env);
    ThrowIllegalAccessException("wrong number of bytes");
  }
  size_t sz = static_cast<size_t>(size);
  memcpy(reinterpret_cast<void *>(dst), reinterpret_cast<void *>(src), sz);
}

template<typename T>
static void copyToArray(jlong srcAddr,
                        ObjPtr<mirror::PrimitiveArray<T>> array,
                        size_t array_offset,
                        size_t size)
        REQUIRES_SHARED(Locks::mutator_lock_) {
  const T* src = reinterpret_cast<T*>(srcAddr);
  size_t sz = size / sizeof(T);
  size_t of = array_offset / sizeof(T);
  for (size_t i = 0; i < sz; ++i) {
    array->Set(i + of, *(src + i));
  }
}

template<typename T>
static void copyFromArray(jlong dstAddr,
                          ObjPtr<mirror::PrimitiveArray<T>> array,
                          size_t array_offset,
                          size_t size)
        REQUIRES_SHARED(Locks::mutator_lock_) {
  T* dst = reinterpret_cast<T*>(dstAddr);
  size_t sz = size / sizeof(T);
  size_t of = array_offset / sizeof(T);
  for (size_t i = 0; i < sz; ++i) {
    *(dst + i) = array->Get(i + of);
  }
}

static void Unsafe_copyMemoryToPrimitiveArray(JNIEnv* env,
                                              [[maybe_unused]] jobject unsafe,
                                              jlong srcAddr,
                                              jobject dstObj,
                                              jlong dstOffset,
                                              jlong size) {
  ScopedObjectAccess soa(env);
  if (size == 0) {
    return;
  }
  // size is nonnegative and fits into size_t
  if (size < 0 || size != static_cast<jlong>(static_cast<size_t>(size))) {
    ThrowIllegalAccessException("wrong number of bytes");
  }
  size_t sz = static_cast<size_t>(size);
  size_t dst_offset = static_cast<size_t>(dstOffset);
  ObjPtr<mirror::Object> dst = soa.Decode<mirror::Object>(dstObj);
  ObjPtr<mirror::Class> component_type = dst->GetClass()->GetComponentType();
  if (component_type->IsPrimitiveByte() || component_type->IsPrimitiveBoolean()) {
    // Note: Treating BooleanArray as ByteArray.
    copyToArray(srcAddr, ObjPtr<mirror::ByteArray>::DownCast(dst), dst_offset, sz);
  } else if (component_type->IsPrimitiveShort() || component_type->IsPrimitiveChar()) {
    // Note: Treating CharArray as ShortArray.
    copyToArray(srcAddr, ObjPtr<mirror::ShortArray>::DownCast(dst), dst_offset, sz);
  } else if (component_type->IsPrimitiveInt() || component_type->IsPrimitiveFloat()) {
    // Note: Treating FloatArray as IntArray.
    copyToArray(srcAddr, ObjPtr<mirror::IntArray>::DownCast(dst), dst_offset, sz);
  } else if (component_type->IsPrimitiveLong() || component_type->IsPrimitiveDouble()) {
    // Note: Treating DoubleArray as LongArray.
    copyToArray(srcAddr, ObjPtr<mirror::LongArray>::DownCast(dst), dst_offset, sz);
  } else {
    ThrowIllegalAccessException("not a primitive array");
  }
}

static void Unsafe_copyMemoryFromPrimitiveArray(JNIEnv* env,
                                                [[maybe_unused]] jobject unsafe,
                                                jobject srcObj,
                                                jlong srcOffset,
                                                jlong dstAddr,
                                                jlong size) {
  ScopedObjectAccess soa(env);
  if (size == 0) {
    return;
  }
  // size is nonnegative and fits into size_t
  if (size < 0 || size != static_cast<jlong>(static_cast<size_t>(size))) {
    ThrowIllegalAccessException("wrong number of bytes");
  }
  size_t sz = static_cast<size_t>(size);
  size_t src_offset = static_cast<size_t>(srcOffset);
  ObjPtr<mirror::Object> src = soa.Decode<mirror::Object>(srcObj);
  ObjPtr<mirror::Class> component_type = src->GetClass()->GetComponentType();
  if (component_type->IsPrimitiveByte() || component_type->IsPrimitiveBoolean()) {
    // Note: Treating BooleanArray as ByteArray.
    copyFromArray(dstAddr, ObjPtr<mirror::ByteArray>::DownCast(src), src_offset, sz);
  } else if (component_type->IsPrimitiveShort() || component_type->IsPrimitiveChar()) {
    // Note: Treating CharArray as ShortArray.
    copyFromArray(dstAddr, ObjPtr<mirror::ShortArray>::DownCast(src), src_offset, sz);
  } else if (component_type->IsPrimitiveInt() || component_type->IsPrimitiveFloat()) {
    // Note: Treating FloatArray as IntArray.
    copyFromArray(dstAddr, ObjPtr<mirror::IntArray>::DownCast(src), src_offset, sz);
  } else if (component_type->IsPrimitiveLong() || component_type->IsPrimitiveDouble()) {
    // Note: Treating DoubleArray as LongArray.
    copyFromArray(dstAddr, ObjPtr<mirror::LongArray>::DownCast(src), src_offset, sz);
  } else {
    ThrowIllegalAccessException("not a primitive array");
  }
}
static jboolean Unsafe_getBoolean(JNIEnv* env, jobject, jobject javaObj, jlong offset) {
  ScopedFastNativeObjectAccess soa(env);
  ObjPtr<mirror::Object> obj = soa.Decode<mirror::Object>(javaObj);
  return obj->GetFieldBoolean(MemberOffset(offset));
}

static void Unsafe_putBoolean(JNIEnv* env, jobject, jobject javaObj, jlong offset, jboolean newValue) {
  ScopedFastNativeObjectAccess soa(env);
  ObjPtr<mirror::Object> obj = soa.Decode<mirror::Object>(javaObj);
  // JNI must use non transactional mode (SetField8 is non-transactional).
  obj->SetFieldBoolean<false>(MemberOffset(offset), newValue);
}

static jbyte Unsafe_getByte(JNIEnv* env, jobject, jobject javaObj, jlong offset) {
  ScopedFastNativeObjectAccess soa(env);
  ObjPtr<mirror::Object> obj = soa.Decode<mirror::Object>(javaObj);
  return obj->GetFieldByte(MemberOffset(offset));
}

static void Unsafe_putByte(JNIEnv* env, jobject, jobject javaObj, jlong offset, jbyte newValue) {
  ScopedFastNativeObjectAccess soa(env);
  ObjPtr<mirror::Object> obj = soa.Decode<mirror::Object>(javaObj);
  // JNI must use non transactional mode.
  obj->SetFieldByte<false>(MemberOffset(offset), newValue);
}

static jchar Unsafe_getChar(JNIEnv* env, jobject, jobject javaObj, jlong offset) {
  ScopedFastNativeObjectAccess soa(env);
  ObjPtr<mirror::Object> obj = soa.Decode<mirror::Object>(javaObj);
  return obj->GetFieldChar(MemberOffset(offset));
}

static void Unsafe_putChar(JNIEnv* env, jobject, jobject javaObj, jlong offset, jchar newValue) {
  ScopedFastNativeObjectAccess soa(env);
  ObjPtr<mirror::Object> obj = soa.Decode<mirror::Object>(javaObj);
  // JNI must use non transactional mode.
  obj->SetFieldChar<false>(MemberOffset(offset), newValue);
}

static jshort Unsafe_getShort(JNIEnv* env, jobject, jobject javaObj, jlong offset) {
  ScopedFastNativeObjectAccess soa(env);
  ObjPtr<mirror::Object> obj = soa.Decode<mirror::Object>(javaObj);
  return obj->GetFieldShort(MemberOffset(offset));
}

static void Unsafe_putShort(JNIEnv* env, jobject, jobject javaObj, jlong offset, jshort newValue) {
  ScopedFastNativeObjectAccess soa(env);
  ObjPtr<mirror::Object> obj = soa.Decode<mirror::Object>(javaObj);
  // JNI must use non transactional mode.
  obj->SetFieldShort<false>(MemberOffset(offset), newValue);
}

static jfloat Unsafe_getFloat(JNIEnv* env, jobject, jobject javaObj, jlong offset) {
  ScopedFastNativeObjectAccess soa(env);
  ObjPtr<mirror::Object> obj = soa.Decode<mirror::Object>(javaObj);
  union {int32_t val; jfloat converted;} conv;
  conv.val = obj->GetField32(MemberOffset(offset));
  return conv.converted;
}

static void Unsafe_putFloat(JNIEnv* env, jobject, jobject javaObj, jlong offset, jfloat newValue) {
  ScopedFastNativeObjectAccess soa(env);
  ObjPtr<mirror::Object> obj = soa.Decode<mirror::Object>(javaObj);
  union {int32_t converted; jfloat val;} conv;
  conv.val = newValue;
  // JNI must use non transactional mode.
  obj->SetField32<false>(MemberOffset(offset), conv.converted);
}

static jdouble Unsafe_getDouble(JNIEnv* env, jobject, jobject javaObj, jlong offset) {
  ScopedFastNativeObjectAccess soa(env);
  ObjPtr<mirror::Object> obj = soa.Decode<mirror::Object>(javaObj);
  union {int64_t val; jdouble converted;} conv;
  conv.val = obj->GetField64(MemberOffset(offset));
  return conv.converted;
}

static void Unsafe_putDouble(JNIEnv* env, jobject, jobject javaObj, jlong offset, jdouble newValue) {
  ScopedFastNativeObjectAccess soa(env);
  ObjPtr<mirror::Object> obj = soa.Decode<mirror::Object>(javaObj);
  union {int64_t converted; jdouble val;} conv;
  conv.val = newValue;
  // JNI must use non transactional mode.
  obj->SetField64<false>(MemberOffset(offset), conv.converted);
}

static void Unsafe_loadFence(JNIEnv*, jobject) {
  std::atomic_thread_fence(std::memory_order_acquire);
}

static void Unsafe_storeFence(JNIEnv*, jobject) {
  std::atomic_thread_fence(std::memory_order_release);
}

static void Unsafe_fullFence(JNIEnv*, jobject) {
  std::atomic_thread_fence(std::memory_order_seq_cst);
}

static void Unsafe_park(JNIEnv* env, jobject, jboolean isAbsolute, jlong time) {
  ScopedObjectAccess soa(env);
  Thread::Current()->Park(isAbsolute, time);
}

static void Unsafe_unpark(JNIEnv* env, jobject, jobject jthread) {
  art::ScopedFastNativeObjectAccess soa(env);
  ObjPtr<mirror::Object> mirror_thread = soa.Decode<mirror::Object>(jthread);
  if (mirror_thread == nullptr ||
      !mirror_thread->InstanceOf(WellKnownClasses::java_lang_Thread.Get())) {
    ThrowIllegalArgumentException("Argument to unpark() was not a Thread");
    return;
  }
  Thread* self = soa.Self();
  art::MutexLock mu(self, *art::Locks::thread_list_lock_);
  art::Thread* thread = art::Thread::FromManagedThread(self, mirror_thread);
  if (thread != nullptr) {
    thread->Unpark();
  } else {
    // If thread is null, that means that either the thread is not started yet,
    // or the thread has already terminated. Setting the field to true will be
    // respected when the thread does start, and is harmless if the thread has
    // already terminated.
    ArtField* unparked = WellKnownClasses::java_lang_Thread_unparkedBeforeStart;
    // JNI must use non transactional mode.
    unparked->SetBoolean<false>(mirror_thread, JNI_TRUE);
  }
}

static const JNINativeMethod gMethods[] = {
  FAST_NATIVE_METHOD(Unsafe, compareAndSwapInt, "(Ljava/lang/Object;JII)Z"),
  FAST_NATIVE_METHOD(Unsafe, compareAndSwapLong, "(Ljava/lang/Object;JJJ)Z"),
  FAST_NATIVE_METHOD(Unsafe, compareAndSwapObject, "(Ljava/lang/Object;JLjava/lang/Object;Ljava/lang/Object;)Z"),
  FAST_NATIVE_METHOD(Unsafe, getIntVolatile, "(Ljava/lang/Object;J)I"),
  FAST_NATIVE_METHOD(Unsafe, putIntVolatile, "(Ljava/lang/Object;JI)V"),
  FAST_NATIVE_METHOD(Unsafe, getLongVolatile, "(Ljava/lang/Object;J)J"),
  FAST_NATIVE_METHOD(Unsafe, putLongVolatile, "(Ljava/lang/Object;JJ)V"),
  FAST_NATIVE_METHOD(Unsafe, getObjectVolatile, "(Ljava/lang/Object;J)Ljava/lang/Object;"),
  FAST_NATIVE_METHOD(Unsafe, putObjectVolatile, "(Ljava/lang/Object;JLjava/lang/Object;)V"),
  FAST_NATIVE_METHOD(Unsafe, getInt, "(Ljava/lang/Object;J)I"),
  FAST_NATIVE_METHOD(Unsafe, putInt, "(Ljava/lang/Object;JI)V"),
  FAST_NATIVE_METHOD(Unsafe, putOrderedInt, "(Ljava/lang/Object;JI)V"),
  FAST_NATIVE_METHOD(Unsafe, getLong, "(Ljava/lang/Object;J)J"),
  FAST_NATIVE_METHOD(Unsafe, putLong, "(Ljava/lang/Object;JJ)V"),
  FAST_NATIVE_METHOD(Unsafe, putOrderedLong, "(Ljava/lang/Object;JJ)V"),
  FAST_NATIVE_METHOD(Unsafe, getObject, "(Ljava/lang/Object;J)Ljava/lang/Object;"),
  FAST_NATIVE_METHOD(Unsafe, putObject, "(Ljava/lang/Object;JLjava/lang/Object;)V"),
  FAST_NATIVE_METHOD(Unsafe, putOrderedObject, "(Ljava/lang/Object;JLjava/lang/Object;)V"),
  FAST_NATIVE_METHOD(Unsafe, getArrayBaseOffsetForComponentType, "(Ljava/lang/Class;)I"),
  FAST_NATIVE_METHOD(Unsafe, getArrayIndexScaleForComponentType, "(Ljava/lang/Class;)I"),
  FAST_NATIVE_METHOD(Unsafe, addressSize, "()I"),
  FAST_NATIVE_METHOD(Unsafe, pageSize, "()I"),
  FAST_NATIVE_METHOD(Unsafe, allocateMemory, "(J)J"),
  FAST_NATIVE_METHOD(Unsafe, freeMemory, "(J)V"),
  FAST_NATIVE_METHOD(Unsafe, setMemory, "(JJB)V"),
  FAST_NATIVE_METHOD(Unsafe, copyMemory, "(JJJ)V"),
  FAST_NATIVE_METHOD(Unsafe, copyMemoryToPrimitiveArray, "(JLjava/lang/Object;JJ)V"),
  FAST_NATIVE_METHOD(Unsafe, copyMemoryFromPrimitiveArray, "(Ljava/lang/Object;JJJ)V"),
  FAST_NATIVE_METHOD(Unsafe, getBoolean, "(Ljava/lang/Object;J)Z"),

  FAST_NATIVE_METHOD(Unsafe, getByte, "(Ljava/lang/Object;J)B"),
  FAST_NATIVE_METHOD(Unsafe, getChar, "(Ljava/lang/Object;J)C"),
  FAST_NATIVE_METHOD(Unsafe, getShort, "(Ljava/lang/Object;J)S"),
  FAST_NATIVE_METHOD(Unsafe, getFloat, "(Ljava/lang/Object;J)F"),
  FAST_NATIVE_METHOD(Unsafe, getDouble, "(Ljava/lang/Object;J)D"),
  FAST_NATIVE_METHOD(Unsafe, putBoolean, "(Ljava/lang/Object;JZ)V"),
  FAST_NATIVE_METHOD(Unsafe, putByte, "(Ljava/lang/Object;JB)V"),
  FAST_NATIVE_METHOD(Unsafe, putChar, "(Ljava/lang/Object;JC)V"),
  FAST_NATIVE_METHOD(Unsafe, putShort, "(Ljava/lang/Object;JS)V"),
  FAST_NATIVE_METHOD(Unsafe, putFloat, "(Ljava/lang/Object;JF)V"),
  FAST_NATIVE_METHOD(Unsafe, putDouble, "(Ljava/lang/Object;JD)V"),
  FAST_NATIVE_METHOD(Unsafe, unpark, "(Ljava/lang/Object;)V"),
  NATIVE_METHOD(Unsafe, park, "(ZJ)V"),

  // Each of the getFoo variants are overloaded with a call that operates
  // directively on a native pointer.
  OVERLOADED_FAST_NATIVE_METHOD(Unsafe, getByte, "(J)B", getByteJ),
  OVERLOADED_FAST_NATIVE_METHOD(Unsafe, getChar, "(J)C", getCharJ),
  OVERLOADED_FAST_NATIVE_METHOD(Unsafe, getShort, "(J)S", getShortJ),
  OVERLOADED_FAST_NATIVE_METHOD(Unsafe, getInt, "(J)I", getIntJ),
  OVERLOADED_FAST_NATIVE_METHOD(Unsafe, getLong, "(J)J", getLongJ),
  OVERLOADED_FAST_NATIVE_METHOD(Unsafe, getFloat, "(J)F", getFloatJ),
  OVERLOADED_FAST_NATIVE_METHOD(Unsafe, getDouble, "(J)D", getDoubleJ),
  OVERLOADED_FAST_NATIVE_METHOD(Unsafe, putByte, "(JB)V", putByteJB),
  OVERLOADED_FAST_NATIVE_METHOD(Unsafe, putChar, "(JC)V", putCharJC),
  OVERLOADED_FAST_NATIVE_METHOD(Unsafe, putShort, "(JS)V", putShortJS),
  OVERLOADED_FAST_NATIVE_METHOD(Unsafe, putInt, "(JI)V", putIntJI),
  OVERLOADED_FAST_NATIVE_METHOD(Unsafe, putLong, "(JJ)V", putLongJJ),
  OVERLOADED_FAST_NATIVE_METHOD(Unsafe, putFloat, "(JF)V", putFloatJF),
  OVERLOADED_FAST_NATIVE_METHOD(Unsafe, putDouble, "(JD)V", putDoubleJD),

  // CAS
  FAST_NATIVE_METHOD(Unsafe, loadFence, "()V"),
  FAST_NATIVE_METHOD(Unsafe, storeFence, "()V"),
  FAST_NATIVE_METHOD(Unsafe, fullFence, "()V"),
};

void register_sun_misc_Unsafe(JNIEnv* env) {
  REGISTER_NATIVE_METHODS("sun/misc/Unsafe");
}

}  // namespace art
