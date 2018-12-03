/*
 * Copyright (C) 2009 The Android Open Source Project
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

#define LOG_TAG "StageFright_HW"
#include <cutils/log.h>
#include "MrvlOMXPlugin.h"

#include <mvmem.h>
#include <dlfcn.h>

#include <media/hardware/HardwareAPI.h>

struct gcuContext;

class libgcu
{
    private:
    template<typename T>
    void load_func(T& x, const char* func)
    {
        *(int*)x = (int)dlsym(_lib, func);
    }

    void *_lib;

    libgcu():_lib(dlopen("libgcu.so", RTLD_NOW))
    {
        load_func(gcuDestroyContext, "gcuDestroyContext");
        load_func(gcuTerminate, "gcuTerminate");
        load_func(gcuDestroyBuffer, "gcuDestroyBuffer");
    }
    public:
    static libgcu& Inst()
    {
        static libgcu i;
        return i;
    }

    void (*gcuDestroyContext)(gcuContext**);
    void (*gcuTerminate)();
    void (*gcuDestroyBuffer)(gcuContext**, int);

};

namespace android {

#define OMX_IndexParamMarvellVmetaDec              ((OMX_INDEXTYPE)(OMX_IndexVendorStartUnused|0x80000012))
#define OMX_IndexParamMarvellAacEnc                ((OMX_INDEXTYPE)(OMX_IndexVendorStartUnused|0x8000016))
#define OMX_IndexParamMarvellStoreMetaInOutputBuff ((OMX_INDEXTYPE)(OMX_IndexVendorStartUnused|0x8000024))


struct struc_1
{
  OMX_BUFFERHEADERTYPE *pBuffer;
  int field_4;
  OMX_BUFFERHEADERTYPE bufferHeader;
  int field_58;
  int field_5C;
  int field_60;
  int field_64;
  int field_68;
  int field_6C;
};

struct MrvlComponentType
{
    OMX_COMPONENTTYPE type;
    char name[128];
    OMX_ERRORTYPE (*EventHandler)(OMX_HANDLETYPE hComponent, OMX_PTR pAppData, OMX_EVENTTYPE eEvent, OMX_U32 nData1, OMX_U32 nData2, OMX_PTR pEventData);
    OMX_ERRORTYPE (*EmptyBufferDone)(OMX_HANDLETYPE hComponent, OMX_PTR pAppData, OMX_BUFFERHEADERTYPE *pBuffer);
    OMX_ERRORTYPE (*FillBufferDone)(OMX_HANDLETYPE hComponent, OMX_PTR pAppData, OMX_BUFFERHEADERTYPE *pBuffer);
    int field_E4;
    struct gcuContext *context;
    int field_EC;
    struc_1 buffers[32];
    int numBuffers;
    int field_EF4;
};

OMX_ERRORTYPE UseBuffer(OMX_HANDLETYPE hComponent, OMX_BUFFERHEADERTYPE **ppBufferHdr, OMX_U32 nPortIndex, OMX_PTR pAppPrivate, OMX_U32 nSizeBytes, OMX_U8 *pBuffer)
{
    MrvlComponentType *compo = (MrvlComponentType*)hComponent;
    MrvlComponentType *privateComponent;
    OMX_ERRORTYPE err;

    if( !compo )
        return OMX_ErrorInvalidComponent;

    err = privateComponent->type.UseBuffer(privateComponent, ppBufferHdr, nPortIndex, nAppPrivate, nSizeBytes, pBuffer);

    if( err = OMX_ErrorNone && compo->field_E4 == 1 && nPortIndex == 0 )
    {
        int numBuffers = compo->numBuffers;
        if( numBuffers < 32 )
        {
            struc_1 *pstruc = &compo->buffers[numBuffers];
            pstruc->pBuffer = *ppBufferHdr;
            memcpy(&pstruc->bufferHeader, *ppBufferHdr, sizeof(struc1));
            ++compo->numBuffers;
            err = OMX_ErrorNone;
        }
        else
        {
            ALOGE("Input port buffer count exceeds max count %d, please check!", 32);
            err = OMX_ErrorInsufficientResources;
        }
    }
    return err;
}

OMX_ERRORTYPE SendCommand(OMX_HANDLETYPE hComponent, OMX_COMMANDTYPE Cmd, OMX_U32 nParam1, OMX_PTR pCmdData)
{
    MrvlComponentType *compo = (MrvlComponentType*)hComponent;
    MrvlComponentType *privateComponent;

    if( !compo )
        return OMX_ErrorInvalidComponent;

    return privateComponent->type.SendCommand(privateComponent, Cmd, nParam1, pCmdData);
}

OMX_ERRORTYPE SetConfig(OMX_HANDLETYPE hComponent, OMX_INDEXTYPE nIndex, OMX_PTR pComponentConfigStructure)
{
    MrvlComponentType *compo = (MrvlComponentType*)hComponent;
    MrvlComponentType *privateComponent;

    if( !compo )
        return OMX_ErrorInvalidComponent;

    return privateComponent->type.SetConfig(privateComponent, nIndex, pComponentConfigStructure);
}

OMX_ERRORTYPE ComponentRoleEnum(OMX_HANDLETYPE hComponent, OMX_U8 *cRole, OMX_U32 nIndex)
{
    MrvlComponentType *compo = (MrvlComponentType*)hComponent;
    MrvlComponentType *privateComponent;

    if( !compo )
        return OMX_ErrorInvalidComponent;

    return privateComponent->type.ComponentRoleEnum(privateComponent, cRole, nIndex);
}

OMX_ERRORTYPE AllocateBuffer(OMX_HANDLETYPE hComponent, OMX_BUFFERHEADERTYPE **ppBuffer, OMX_U32 nPortIndex, OMX_PTR pAppPrivate, OMX_U32 nSizeBytes)
{
    MrvlComponentType *compo = (MrvlComponentType*)hComponent;
    MrvlComponentType *privateComponent;
    OMX_ERRORTYPE err;
    if ( !compo )
        return OMX_ErrorInvalidComponent;

    privateComponent = (MrvlComponentType*)compo->type.pComponentPrivate;
    err = privateComponent->type.AllocateBuffer(privateComponent, ppBuffer, nPortIndex, pAppPrivate, nSizeBytes);
    if( err == OMX_ErrorNone && compo->field_E4 == 1 && !nPortIndex )
    {
        int numBuffers = compo->numBuffers;
        if( numBuffers < 32 )
        {
            struc_1 *pstruc = &compo->buffers[numBuffers];
            pstruc->pBuffer = *ppBuffer;
            memcpy(&pstruc->bufferHeader, *ppBuffer, sizeof(struc1));
            ++compo->numBuffers;
            err = OMX_ErrorNone;
        }
        else
        {
            ALOGE("Input port buffer count exceeds max count %d, please check!", 32);
            err = OMX_ErrorInsufficientResources;
        }
    }

    return err;
}

OMX_ERRORTYPE EmptyBufferDone(OMX_HANDLETYPE hComponent, OMX_PTR pAppData, OMX_BUFFERHEADERTYPE *pBuffer)
{
    MrvlComponentType *compo = (MrvlComponentType*)hComponent;
    if ( !compo )
        return OMX_ErrorInvalidComponent;

    compo = (MrvlComponentType*)compo->type.pApplicationPrivate;

    if ( compo->field_E4 == 1 )
    {
        struc_1 *v5;
        for( int i = 0; i < 32; ++i )
        {
            v5 = &compo->buffers[i];
            if ( compo->buffers[i].pBuffer == pBuffer )
                break;
            if ( i == 31 )
            {
                ALOGE("Could not find backup input port buffer header!.\n");
                return OMX_ErrorUndefined;
            }
        }
        pBuffer->pBuffer   = v5->bufferHeader.pBuffer;
        pBuffer->nAllocLen = v5->bufferHeader.nAllocLen;
        pBuffer->nOffset   = v5->bufferHeader.nOffset;
    }
    return compo->EmptyBufferDone(compo, pAppData, pBuffer);
}

OMX_ERRORTYPE EventHandler(OMX_HANDLETYPE hComponent, OMX_PTR pAppData, OMX_EVENTTYPE eEvent, OMX_U32 nData1, OMX_U32 nData2, OMX_PTR pEventData)
{
    MrvlComponentType *compo = (MrvlComponentType*)hComponent;
    if ( !compo )
        return OMX_ErrorInvalidComponent;

    compo = (MrvlComponentType*)compo->type.pApplicationPrivate;
    return compo->EventHandler(compo, pAppData, eEvent, nData1, nData2, pEventData);
}

OMX_ERRORTYPE FillThisBuffer(OMX_HANDLETYPE hComponent, OMX_BUFFERHEADERTYPE *pBuffer)
{
    MrvlComponentType *compo = (MrvlComponentType*)hComponent;
    if ( !compo )
        return OMX_ErrorInvalidComponent;

    compo = (MrvlComponentType*)compo->type.pComponentPrivate;
    return compo->type.FillThisBuffer(compo, pBuffer);
}

OMX_ERRORTYPE GetComponentType(OMX_HANDLETYPE hComponent, OMX_STRING pComponentName, OMX_VERSIONTYPE *pComponentVersion, OMX_VERSIONTYPE *pSpecVersion, OMX_UUIDTYPE *pComponentUUID)
{
    MrvlComponentType *compo = (MrvlComponentType*)hComponent;
    if ( !compo )
        return OMX_ErrorInvalidComponent;

    compo = (MrvlComponentType*)compo->type.pComponentPrivate;
    return compo->type.GetComponentVersion(compo, pComponentName, pComponentVersion, pSpecVersion, pComponentUUID);
}

OMX_ERRORTYPE GetConfig(OMX_HANDLETYPE hComponent, OMX_INDEXTYPE nIndex, OMX_PTR pComponentConfigStructure)
{
    MrvlComponentType *compo = (MrvlComponentType*)hComponent;
    if ( !compo )
        return OMX_ErrorInvalidComponent;

    compo = (MrvlComponentType*)compo->type.pComponentPrivate;
    return compo->type.GetConfig(compo, nIndex, pComponentConfigStructure);
}

OMX_ERRORTYPE GetExtension(OMX_HANDLETYPE hComponent, OMX_STRING cParameterName, OMX_INDEXTYPE *pInOMX_INDEXTYPE)
{
    MrvlComponentType *compo = (MrvlComponentType*)hComponent;
    if ( !compo )
        return OMX_ErrorInvalidComponent;

    compo = (MrvlComponentType*)compo->type.pComponentPrivate;
    return compo->type.GetExtensionIndex(compo, cParameterName, pInOMX_INDEXTYPE);
}

OMX_ERRORTYPE GetState(OMX_HANDLETYPE hComponent, OMX_STATETYPE *pState)
{
    MrvlComponentType *compo = (MrvlComponentType*)hComponent;
    if ( !compo )
        return OMX_ErrorInvalidComponent;

    compo = (MrvlComponentType*)compo->type.pComponentPrivate;
    return compo->type.GetState(compo, pState);
}

OMX_ERRORTYPE GetParameter(OMX_HANDLETYPE hComponent, OMX_INDEXTYPE nParamIndex, OMX_PTR pComponentParameterStructure)
{
    MrvlComponentType *compo = (MrvlComponentType*)hComponent;
    if ( !compo )
        return OMX_ErrorInvalidComponent;

    compo = (MrvlComponentType*)compo->type.pComponentPrivate;
    return compo->type.GetParameter(compo, nParamIndex, pComponentParameterStructure);
}

OMX_ERRORTYPE FillBufferDone(OMX_HANDLETYPE component, OMX_PTR pAppData, OMX_BUFFERHEADERTYPE *pBuffer)
{
    MrvlComponentType *compo = (MrvlComponentType*)component;
    if( !compo )
        return OMX_ErrorInvalidComponent;

    compo = (MrvlComponentType*)compo->type.pApplicationPrivate;
    return compo->FillBufferDone(compo, pAppData, pBuffer);
}

OMX_ERRORTYPE SetParameter(OMX_HANDLETYPE hComponent, OMX_INDEXTYPE nIndex, OMX_PTR pComponentParameters)
{
    OMX_ERRORTYPE res;
    MrvlComponentType *compo = (MrvlComponentType*)hComponent;
    MrvlComponentType *privateCompo;
    int params[4];

    if( !compo )
        return OMX_ErrorInvalidComponent;

    privateCompo = (MrvlComponentType*)compo->type.pComponentPrivate;

    if( nIndex != OMX_IndexParamMarvellStoreMetaInOutputBuff )
        return privateCompo->type.SetParameter(privateCompo, nIndex, pComponentParameters);

    if( *((int*)pComponentParameters + 2) )
    {
        ALOGI("%s: currently we do not support StoreMetaDataInOutputBuffers usage.", compo->name);
        return OMX_ErrorNotImplemented;
    }

    compo->field_E4 = *((int*)pComponentParameters + 3);
    params[0] = *((int*)pComponentParameters);
    params[1] = *((int*)pComponentParameters + 1);
    params[2] = *((int*)pComponentParameters + 2);
    params[3] = *((int*)pComponentParameters + 3);

    res = privateCompo->type.SetParameter(privateCompo, OMX_IndexParamMarvellStoreMetaInOutputBuff, params);
    if( res != OMX_ErrorNone )
    {
        ALOGE("%s: SetParameter OMX_IndexParamMarvellStoreMetaData failed, error = 0x%x", compo->name, res);
    }
    return res;
}

OMXPluginBase *createOMXPlugin()
{
    return new OMXMRVLCodecsPlugin;
}

OMXMRVLCodecsPlugin::OMXMRVLCodecsPlugin():
    mLibHandle(dlopen("libMrvlOmx.so", RTLD_NOW)),
    mInit(NULL),
    mDeinit(NULL),
    mComponentNameEnum(NULL),
    mGetHandle(NULL),
    mFreeHandle(NULL),
    mGetRolesOfComponentHandle(NULL)
{
    if (mLibHandle != NULL)
    {
        mInit = (InitFunc)dlsym(mLibHandle, "OMX_Init");
        mDeinit = (DeinitFunc)dlsym(mLibHandle, "OMX_Deinit");

        mComponentNameEnum =
            (ComponentNameEnumFunc)dlsym(mLibHandle, "OMX_ComponentNameEnum");

        mGetHandle = (GetHandleFunc)dlsym(mLibHandle, "OMX_GetHandle");
        mFreeHandle = (FreeHandleFunc)dlsym(mLibHandle, "OMX_FreeHandle");

        mGetRolesOfComponentHandle =
            (GetRolesOfComponentFunc)dlsym(
                    mLibHandle, "OMX_GetRolesOfComponent");

        (*mInit)();
    }
}

OMXMRVLCodecsPlugin::~OMXMRVLCodecsPlugin()
{
    if (mLibHandle != NULL)
    {
        (*mDeinit)();

        dlclose(mLibHandle);
        mLibHandle = NULL;
    }
}

OMX_ERRORTYPE OMXMRVLCodecsPlugin::makeComponentInstance(
        const char *name,
        const OMX_CALLBACKTYPE *callbacks,
        OMX_PTR appData,
        OMX_COMPONENTTYPE **component)
{
    OMX_CALLBACKTYPE  callbacksa;
    OMX_ERRORTYPE res;
    MrvlComponentType *privateComponent = NULL;
    MrvlComponentType *_component;
    int v22[20];

    callbacksa.FillBufferDone  = FillBufferDone;
    callbacksa.EmptyBufferDone = EmptyBufferDone;
    callbacksa.EventHandler    = EventHandler;

    _component = (MrvlComponentType*)malloc(sizeof(MrvlComponentType));
    if( _component == NULL )
        return OMX_ErrorInsufficientResources;

    res = mGetHandle((void**)&privateComponent, const_cast<OMX_STRING>(name), appData, &callbacksa);
    if( res != OMX_ErrorNone )
    {
        ALOGE("OMX_GetHandle failed %s (ret=0x%x)", name, res);
        free(_component);
        *component = NULL;
        return res;
    }

    *component = (OMX_COMPONENTTYPE*)_component;
    _component->type.GetComponentVersion = GetComponentType;
    _component->type.SendCommand = SendCommand;
    _component->type.GetParameter = GetParameter;
    _component->type.SetParameter = SetParameter;
    _component->type.GetConfig = GetConfig;
    _component->type.SetConfig = SetConfig;
    _component->type.GetExtensionIndex = GetExtension;
    _component->type.GetState = GetState;
    _component->type.UseBuffer = UseBuffer;
    _component->type.AllocateBuffer = AllocateBuffer;
    _component->type.FreeBuffer = FreeBuffer;
    _component->type.EmptyThisBuffer = EmptyThisBuffer;
    _component->type.FillThisBuffer = FillThisBuffer;
    _component->type.UseEGLImage = UseEGLImage;
    _component->type.ComponentRoleEnum = ComponentRoleEnum;
    _component->EmptyBufferDone = callbacks->EmptyBufferDone;
    _component->FillBufferDone = callbacks->FillBufferDone;
    _component->EventHandler = callbacks->EventHandler;
    _component->field_E4 = 0;
    _component->gcuContext = NULL;
    _component->field_EC = 0;
    _component->numBuffers = 0;


    for( int i = 0; i < 32; ++i )
    {
        int **x = (int**)_component->buffers[i].field_58;
        if( x )
            ; // android::RefBase::decStrong((char*)x + *(*x-3), x);
        memset(&_component->buffers[i].bufferHeader, 0, sizeof(int)*20);
        _component->buffers[i].field_58 = 0;
        _component->buffers[i].field_5C = 0;
        _component->buffers[i].field_60 = 0;
        _component->buffers[i].field_64 = 0;
        _component->buffers[i].field_68 = 0;
    }

    privateComponent->type.pApplicationPrivate = _component;
    _component->type.pComponentPrivate = privateComponent;

    strncpy(_component->name, name, 128);
    if( !strcmp(name, "OMX.MARVELL.AUDIO.AACENCODER") )
    {
        v22[0] = 16;
        v22[2] = 1;
        v22[3] = 1;
        res = privateComponent->type.SetParameter(privateComponent, OMX_IndexParamMarvellAacEnc, v22);
    }

    if( strcmp(name, "OMX.MARVELL.VIDEO.VMETADECODER") )
        return res;

    v22[0] = 40;
    v22[1] = 1;
    res = privateComponent->type.GetParameter(privateComponent, OMX_IndexParamMarvellVmetaDec, v22);
    if( res != OMX_ErrorNone )
    {
        ALOGE("Get parameter OMX_IndexParamMarvellVmetaDec failed, error = 0x%x", res);
    }
    else
    {
        v22[6] &= 0x7FFFFFFE;
        res = privateComponent->type.SetParameter(privateComponent, OMX_IndexParamMarvellVmetaDec, v22);
        if( res == OMX_ErrorNone )
        {
            ALOGI("PLATFORM_SDK_VERSION = %d, ENABLE_ADVANAVSYNC_1080P = 0x%lx, ENABLE_POWEROPT = 0x%lx", 22, v22[6]&1, v22[6]&0x80000000);
        }
        else
        {
            ALOGE("Set parameter OMX_IndexParamMarvellVmetaDec failed, error = 0x%x", res);
        }
    }

    return res;
}

OMX_ERRORTYPE OMXMRVLCodecsPlugin::destroyComponentInstance(OMX_COMPONENTTYPE *component)
{
    if (mLibHandle == NULL) {
        return OMX_ErrorUndefined;
    }

    MrvlComponentType *comp = (MrvlComponentType*)component;
    OMX_ERRORTYPE res;
    if( comp->gcuContext)
    {
        libgcu::Inst().gcuDestroyContext(&comp->gcuContext);
        libgcu::Inst().gcuTerminate();
        comp->gcuContext = 0;
    }

    res = mFreeHandle((OMX_HANDLETYPE*)comp->type.pComponentPrivate);
    free(component);
    return res;
}

OMX_ERRORTYPE OMXMRVLCodecsPlugin::enumerateComponents(
        OMX_STRING name,
        size_t size,
        OMX_U32 index) {
    if (mLibHandle == NULL) {
        return OMX_ErrorUndefined;
    }

    return (*mComponentNameEnum)(name, size, index);
}

OMX_ERRORTYPE OMXMRVLCodecsPlugin::getRolesOfComponent(
        const char *name,
        Vector<String8> *roles) {
    roles->clear();

    OMX_U32 numRoles;
    OMX_ERRORTYPE err = (*mGetRolesOfComponentHandle)(
            const_cast<OMX_STRING>(name), &numRoles, NULL);

    if (err != OMX_ErrorNone || numRoles == 0) {
        return err;
    }

    if (numRoles > 0) {
        OMX_U8 **array = new OMX_U8 *[numRoles];
        for (OMX_U32 i = 0; i < numRoles; ++i) {
            array[i] = new OMX_U8[OMX_MAX_STRINGNAME_SIZE];
        }

        OMX_U32 numRoles2 = numRoles;
        err = (*mGetRolesOfComponentHandle)(const_cast<OMX_STRING>(name), &numRoles2, array);

    	CHECK_EQ((OMX_U32)err, (OMX_U32)OMX_ErrorNone);
        CHECK_EQ(numRoles, numRoles2);

        for (OMX_U32 i = 0; i < numRoles; ++i) {
            String8 s((const char *)array[i]);
            roles->push(s);

            delete[] array[i];
            array[i] = NULL;
        }

        delete[] array;
        array = NULL;
    }

    return OMX_ErrorNone;
}

}  // namespace android
