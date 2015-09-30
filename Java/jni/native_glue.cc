//
//  native_glue.cpp
//  CBForest
//
//  Created by Jens Alfke on 9/11/15.
//  Copyright © 2015 Couchbase. All rights reserved.
//

#include "native_glue.hh"
#include "c4Database.h"
#include "fdb_errors.h"
#include <assert.h>

using namespace forestdb::jni;


// Will be called by JNI when the library is loaded
JNIEXPORT jint JNICALL
JNI_OnLoad(JavaVM *jvm, void *reserved)
{
    JNIEnv *env;
    if (jvm->GetEnv((void **)&env, JNI_VERSION_1_2) == JNI_OK
            && initDatabase(env)
            && initDocument(env)
            && initQueryIterator(env)
            && initView(env)) {
        assert(gJVM == NULL);
        gJVM = jvm;
        return JNI_VERSION_1_2;
    } else {
        return JNI_ERR;
    }
}


namespace forestdb {
    namespace jni {

        JavaVM *gJVM;
        
        jstringSlice::jstringSlice(JNIEnv *env, jstring js)
        :_env(env),
         _jstr(js)
        {
            jboolean isCopy;
            _cstr = env->GetStringUTFChars(js, &isCopy);
            _slice = slice(_cstr);
        }

        jstringSlice::~jstringSlice() {
            if (_cstr)
                _env->ReleaseStringUTFChars(_jstr, _cstr);
        }


        jbyteArraySlice::jbyteArraySlice(JNIEnv *env, jbyteArray jbytes, bool critical)
        :_env(env),
         _jbytes(jbytes),
         _critical(critical)
        {
            jboolean isCopy;
            if (critical)
                _slice.buf = env->GetPrimitiveArrayCritical(jbytes, &isCopy);
            else
                _slice.buf = env->GetByteArrayElements(jbytes, &isCopy);
            _slice.size = env->GetArrayLength(jbytes);
        }

        jbyteArraySlice::~jbyteArraySlice() {
            if (_slice.buf) {
                if (_critical)
                    _env->ReleasePrimitiveArrayCritical(_jbytes, (void*)_slice.buf, JNI_ABORT);
                else
                    _env->ReleaseByteArrayElements(_jbytes, (jbyte*)_slice.buf, JNI_ABORT);
            }
        }

        alloc_slice jbyteArraySlice::copy(JNIEnv *env, jbyteArray jbytes) {
            jsize size = env->GetArrayLength(jbytes);
            alloc_slice slice(size);
            env->GetByteArrayRegion(jbytes, 0, size, (jbyte*)slice.buf);
            return slice;
        }


        void throwError(JNIEnv *env, C4Error error) {
            jclass xclass = env->FindClass("com/couchbase/cbforest/ForestException");
            assert(xclass); // if we can't even throw an exception, we're really fuxored
            jmethodID m = env->GetMethodID(xclass, "throwError", "(II)");
            assert(m);
            env->CallStaticVoidMethod(xclass, m, (jint)error.domain, (jint)error.code);
        }


        jstring toJString(JNIEnv *env, C4Slice s) {
            if (s.buf == NULL)
                return NULL;
            char utf8Buf[s.size + 1];   // FIX: Use heap if string is too long for stack
            ::memcpy(utf8Buf, s.buf, s.size);
            utf8Buf[s.size] = '\0';
            return env->NewStringUTF(utf8Buf);
        }


        jbyteArray toJByteArray(JNIEnv *env, C4Slice s) {
            if (s.buf == NULL)
                return NULL;
            jbyteArray array = env->NewByteArray((jsize)s.size);
            if (array)
                env->SetByteArrayRegion(array, 0, (jsize)s.size, (const jbyte*)s.buf);
            return array;
        }


        bool getEncryptionKey(JNIEnv *env, jint keyAlg, jbyteArray jKeyBytes,
                              C4EncryptionKey *outKey)
        {
            outKey->algorithm = keyAlg;
            if (keyAlg != kC4EncryptionNone) {
                jbyteArraySlice keyBytes(env, jKeyBytes);
                forestdb::slice keySlice = keyBytes;
                if (!keySlice.buf || keySlice.size > sizeof(outKey->bytes)) {
                    throwError(env, C4Error{ForestDBDomain, FDB_RESULT_CRYPTO_ERROR});
                    return false;
                }
                memset(outkey->bytes, 0, sizeof(outKey->bytes));
                memcpy(outKey->bytes, keySlice.buf, keySlice.size);
            }
            return true;
        }


    }
}
