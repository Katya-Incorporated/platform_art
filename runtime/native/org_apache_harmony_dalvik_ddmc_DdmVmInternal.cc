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

#include "org_apache_harmony_dalvik_ddmc_DdmVmInternal.h"

#include <android-base/logging.h>

#include "base/file_utils.h"
#include "base/mutex.h"
#include "base/endian_utils.h"
#include "debugger.h"
#include "gc/heap.h"
#include "jni/jni_internal.h"
#include "native_util.h"
#include "nativehelper/jni_macros.h"
#include "nativehelper/scoped_local_ref.h"
#include "nativehelper/scoped_primitive_array.h"
#include "scoped_fast_native_object_access-inl.h"
#include "thread_list.h"

namespace art HIDDEN {

static void DdmVmInternal_setRecentAllocationsTrackingEnabled(JNIEnv*, jclass, jboolean enable) {
  Dbg::SetAllocTrackingEnabled(enable);
}

static void DdmVmInternal_setThreadNotifyEnabled(JNIEnv*, jclass, jboolean enable) {
  Dbg::DdmSetThreadNotification(enable);
}

static Thread* GetSelf(JNIEnv* env) {
  return static_cast<JNIEnvExt*>(env)->GetSelf();
}

/*
 * Get a stack trace as an array of StackTraceElement objects.  Returns
 * nullptr on failure, e.g. if the threadId couldn't be found.
 */
static jobjectArray DdmVmInternal_getStackTraceById(JNIEnv* env, jclass, jint thin_lock_id) {
  jobjectArray trace = nullptr;
  Thread* const self = GetSelf(env);
  if (static_cast<uint32_t>(thin_lock_id) == self->GetThreadId()) {
    // No need to suspend ourself to build stacktrace.
    ScopedObjectAccess soa(env);
    jobject internal_trace = soa.AddLocalReference<jobject>(self->CreateInternalStackTrace(soa));
    trace = Thread::InternalStackTraceToStackTraceElementArray(soa, internal_trace);
  } else {
    ThreadList* thread_list = Runtime::Current()->GetThreadList();

    // Check for valid thread
    if (thin_lock_id == ThreadList::kInvalidThreadId) {
      return nullptr;
    }

    // Suspend thread to build stack trace.
    Thread* thread = thread_list->SuspendThreadByThreadId(thin_lock_id, SuspendReason::kInternal);
    if (thread != nullptr) {
      {
        ScopedObjectAccess soa(env);
        jobject internal_trace =
            soa.AddLocalReference<jobject>(thread->CreateInternalStackTrace(soa));
        trace = Thread::InternalStackTraceToStackTraceElementArray(soa, internal_trace);
      }
      // Restart suspended thread.
      bool resumed = thread_list->Resume(thread, SuspendReason::kInternal);
      DCHECK(resumed);
    }
  }
  return trace;
}

static void ThreadCountCallback(Thread*, void* context) {
  uint16_t& count = *reinterpret_cast<uint16_t*>(context);
  ++count;
}

static const int kThstBytesPerEntry = 18;
static const int kThstHeaderLen = 4;

static constexpr uint8_t ToJdwpThreadStatus(ThreadState state) {
  /*
  * ThreadStatus constants.
  */
  enum JdwpThreadStatus : uint8_t {
    TS_ZOMBIE   = 0,
    TS_RUNNING  = 1,  // RUNNING
    TS_SLEEPING = 2,  // (in Thread.sleep())
    TS_MONITOR  = 3,  // WAITING (monitor wait)
    TS_WAIT     = 4,  // (in Object.wait())
  };
  switch (state) {
    case ThreadState::kBlocked:
      return TS_MONITOR;
    case ThreadState::kNative:
    case ThreadState::kRunnable:
    case ThreadState::kSuspended:
      return TS_RUNNING;
    case ThreadState::kObsoleteRunnable:
    case ThreadState::kInvalidState:
      break;  // Obsolete or invalid value.
    case ThreadState::kSleeping:
      return TS_SLEEPING;
    case ThreadState::kStarting:
    case ThreadState::kTerminated:
      return TS_ZOMBIE;
    case ThreadState::kTimedWaiting:
    case ThreadState::kWaitingForTaskProcessor:
    case ThreadState::kWaitingForLockInflation:
    case ThreadState::kWaitingForCheckPointsToRun:
    case ThreadState::kWaitingForDebuggerSend:
    case ThreadState::kWaitingForDebuggerSuspension:
    case ThreadState::kWaitingForDebuggerToAttach:
    case ThreadState::kWaitingForDeoptimization:
    case ThreadState::kWaitingForGcToComplete:
    case ThreadState::kWaitingForGetObjectsAllocated:
    case ThreadState::kWaitingForJniOnLoad:
    case ThreadState::kWaitingForMethodTracingStart:
    case ThreadState::kWaitingForSignalCatcherOutput:
    case ThreadState::kWaitingForVisitObjects:
    case ThreadState::kWaitingInMainDebuggerLoop:
    case ThreadState::kWaitingInMainSignalCatcherLoop:
    case ThreadState::kWaitingPerformingGc:
    case ThreadState::kWaitingWeakGcRootRead:
    case ThreadState::kWaitingForGcThreadFlip:
    case ThreadState::kNativeForAbort:
    case ThreadState::kWaiting:
      return TS_WAIT;
      // Don't add a 'default' here so the compiler can spot incompatible enum changes.
  }
  LOG(FATAL) << "Unknown thread state: " << state;
  UNREACHABLE();
}

static void ThreadStatsGetterCallback(Thread* t, void* context) {
  /*
   * Generate the contents of a THST chunk.  The data encompasses all known
   * threads.
   *
   * Response has:
   *  (1b) header len
   *  (1b) bytes per entry
   *  (2b) thread count
   * Then, for each thread:
   *  (4b) thread id
   *  (1b) thread status
   *  (4b) tid
   *  (4b) utime
   *  (4b) stime
   *  (1b) is daemon?
   *
   * The length fields exist in anticipation of adding additional fields
   * without wanting to break ddms or bump the full protocol version.  I don't
   * think it warrants full versioning.  They might be extraneous and could
   * be removed from a future version.
   */
  char native_thread_state;
  int utime;
  int stime;
  int task_cpu;
  GetTaskStats(t->GetTid(), &native_thread_state, &utime, &stime, &task_cpu);

  std::vector<uint8_t>& bytes = *reinterpret_cast<std::vector<uint8_t>*>(context);
  Append4BE(bytes, t->GetThreadId());
  Append1BE(bytes, ToJdwpThreadStatus(t->GetState()));
  Append4BE(bytes, t->GetTid());
  Append4BE(bytes, utime);
  Append4BE(bytes, stime);
  Append1BE(bytes, t->IsDaemon());
}

static jbyteArray DdmVmInternal_getThreadStats(JNIEnv* env, jclass) {
  std::vector<uint8_t> bytes;
  Thread* self = GetSelf(env);
  {
    MutexLock mu(self, *Locks::thread_list_lock_);
    ThreadList* thread_list = Runtime::Current()->GetThreadList();

    uint16_t thread_count = 0;
    thread_list->ForEach(ThreadCountCallback, &thread_count);

    Append1BE(bytes, kThstHeaderLen);
    Append1BE(bytes, kThstBytesPerEntry);
    Append2BE(bytes, thread_count);

    thread_list->ForEach(ThreadStatsGetterCallback, &bytes);
  }

  jbyteArray result = env->NewByteArray(bytes.size());
  if (result != nullptr) {
    env->SetByteArrayRegion(result, 0, bytes.size(), reinterpret_cast<const jbyte*>(&bytes[0]));
  }
  return result;
}

static const JNINativeMethod gMethods[] = {
  NATIVE_METHOD(DdmVmInternal, setRecentAllocationsTrackingEnabled, "(Z)V"),
  NATIVE_METHOD(DdmVmInternal, setThreadNotifyEnabled, "(Z)V"),
  NATIVE_METHOD(DdmVmInternal, getStackTraceById, "(I)[Ljava/lang/StackTraceElement;"),
  NATIVE_METHOD(DdmVmInternal, getThreadStats, "()[B"),
};

void register_org_apache_harmony_dalvik_ddmc_DdmVmInternal(JNIEnv* env) {
  REGISTER_NATIVE_METHODS("org/apache/harmony/dalvik/ddmc/DdmVmInternal");
}

}  // namespace art
