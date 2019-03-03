/*
 * Copyright (C) 2016 Android For Marvell Project <ctx.xda@gmail.com>
 * Copyright (c) 2009 Marvell International Ltd.
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

#include "stagefright_mrvl_omx_plugin.h"
#include <media/stagefright/foundation/ADebug.h> /* Define CHECK_EQ */
#include "OMX_IppDef.h"
#include <binder/IMemory.h>
#include <binder/MemoryHeapBase.h>
#include <utils/RefBase.h>
#include <sys/ioctl.h>
#include <cutils/properties.h>
#ifdef USE_ION
#include <mvmem.h>
#include <linux/ion.h>
#endif

#ifdef LOG_TAG
#undef LOG_TAG
#endif

#define LOG_TAG "StageFright_HW"
// #define ENABLE_LOG
#ifdef ENABLE_LOG
#define MARVELL_LOG(...) ALOGD(__VA_ARGS__)
#else
#define MARVELL_LOG(...)
#endif

#define DEFAULT_AUDIO_DECODER_INPUT_BUFFER  (5)
#define DEFAULT_AUDIO_DECODER_OUTPUT_BUFFER (9)
#define DEFAULT_VIDEO_DECODER_INPUT_BUFFER  (5)
#define DEFAULT_VIDEO_DECODER_OUTPUT_BUFFER (2)
#define DEFAULT_AUDIO_ENCODER_INPUT_BUFFER (1)
#define DEFAULT_AUDIO_ENCODER_OUTPUT_BUFFER (12)

#include <dlfcn.h>

#include <exception>

#include <MrvlOmx.h>
#include <gpu_csc.h>
#include <gralloc_priv.h>
#include <gc_gralloc_gr.h>
#include <mrvl_pxl_formats.h>

struct OMX_METADATAPARAM
{
  int nSize;
  int field_4;
  int field_8;
  int field_C;
  int field_10;
  int width;
  int height;
  OMX_COLOR_FORMATTYPE format;
  int field_20;
  int field_24;
  int field_28;
  int field_2C;
  int field_30;
  int field_34;
  int field_38;
  int field_3C;
  int field_40;
  int field_44;
  int field_48;
  int field_4C;
  int field_50;
  int field_54;
  int field_58;
  int field_5C;
  int field_60;
  int field_64;
  int field_68;
  int field_6C;
  int field_70;
  int field_74;
  int field_78;
  int field_7C;
  int field_80;
  int field_84;
  int field_88;
  int field_8C;
  int field_90;
  int field_94;
  int field_98;
  int field_9C;
  int field_A0;
  int field_A4;
};


struct struc_1
{
    OMX_BUFFERHEADERTYPE *pBuffer;
    int field_4;
    OMX_BUFFERHEADERTYPE bufferHeader;
    android::sp<android::IMemoryHeap> memHeap;
    int field_5C;
    int field_60;
    int field_64;
    GCUSurface surface;
    int field_6C;
};

typedef struct{
    OMX_COMPONENTTYPE StandardComp;
    OMX_U8 ComponentName[128];
    OMX_CALLBACKTYPE  InternalCallBack;
    int field_E4;
    GCUContext context;
    int field_EC;
    struc_1 buffers[32];
    int numBuffers;
    int field_EF4;
}IppOmxCompomentWrapper_t;

typedef struct _gcBufferAttr
{
  GCUint width;
  GCUint height;
  GCUint left;
  GCUint right;
  GCUint top;
  GCUint bottom;
  GCUVirtualAddr Vaddr;
  GCUPhysicalAddr Paddr;
  GCUSurface *surface;
} gcBufferAttr;

struct libstock
{
    int offset;
    void *_lib;
    OMX_ERRORTYPE (*storeMetaDataInBufferHandling)(IppOmxCompomentWrapper_t *hComponent, OMX_BUFFERHEADERTYPE_IPPEXT *pBuffer);

    libstock():_lib(dlopen("libstagefrighthw_stock.so", RTLD_NOW))
    {
        offset = (int)dlsym(_lib, "_ZN7android19OMXMRVLCodecsPlugin21makeComponentInstanceEPKcPK16OMX_CALLBACKTYPEPvPP17OMX_COMPONENTTYPE")-0x1ae8;
        *(int*)&storeMetaDataInBufferHandling = 0x2014 + offset;
    }

    static libstock& Inst()
    {
        static libstock lib;
        return lib;
    }
};


#define IPPOMX_COMPONENT(x) ((IppOmxCompomentWrapper_t*)(x))
#define IPPOMX_PAPPLICATION(x) (IppOmxCompomentWrapper_t*)((OMX_COMPONENTTYPE*)(x))->pApplicationPrivate
#define IPPOMX_PCOMPONENT(x) (IppOmxCompomentWrapper_t*)((OMX_COMPONENTTYPE*)(x))->pComponentPrivate

static OMX_ERRORTYPE gcu_csc(GCUContext context, gcBufferAttr src, GCU_FORMAT srcFormat, gcBufferAttr dst, GCU_FORMAT dstFormat)
{
    GCU_RECT srcRect;
    GCU_RECT dstRect;
    GCU_BLT_DATA bltDatas;
    GCUSurface srcSurface;
    OMX_ERRORTYPE err = (OMX_ERRORTYPE)0x80000000;

    srcRect.top    = src.top;
    srcRect.left   = src.left;
    srcRect.bottom = src.bottom;
    srcRect.right  = src.right;

    dstRect.top    = dst.top;
    dstRect.left   = dst.left;
    dstRect.bottom = dst.bottom;
    dstRect.right  = dst.right;

    if( context == NULL )
    {
        ALOGE("GCU csc: invalid GCU context.");
        return err;
    }

    srcSurface = _gcuCreatePreAllocBuffer(context, src.width, src.height, srcFormat, 1, src.Vaddr, 0, 0);
    if( srcSurface == NULL )
    {
        ALOGE("GCU csc: prepare src surface failed.");
        return err;
    }

    if( !*dst.surface )
        *dst.surface = _gcuCreatePreAllocBuffer(context, dst.width, dst.height, dstFormat, 1, dst.Vaddr, 0, 0);

    if( *dst.surface )
    {
        memset(&bltDatas, 0, sizeof(GCU_BLT_DATA));
        bltDatas.pSrcSurface = srcSurface;
        bltDatas.pSrcRect = &srcRect;

        bltDatas.pDstSurface = *dst.surface;
        bltDatas.pDstRect = &dstRect;

        gcuBlit(context, &bltDatas);
        gcuFinish(context);
        err = OMX_ErrorNone;
    }
    else
    {
        ALOGE("GCU csc: prepare dst surface failed.");
    }

    _gcuDestroyBuffer(context, srcSurface);

    return err;
}

extern OMX_ERRORTYPE storeMetaDataInBufferHandling(IppOmxCompomentWrapper_t *hComponent, OMX_BUFFERHEADERTYPE_IPPEXT *pBuffer)
{

    ALOGE("%s not reversed, falling back to stock function!", __func__);
    return libstock::Inst().storeMetaDataInBufferHandling(hComponent, pBuffer);

    IppOmxCompomentWrapper_t *pComponent = IPPOMX_PCOMPONENT(hComponent);
    OMX_ERRORTYPE error;
    OMX_U32* buffer;
    int gcFormat;
    uint32_t dmaAddr;
    int err;
    GCU_INIT_DATA contextData;
    GCU_CONTEXT_DATA contextData2;
    // This is an OMX structure, but which one...
    OMX_METADATAPARAM pComponentConfigStructure;
    struc_1 *pstruc;
    int size;
    const char *gcuString;

    if( hComponent == NULL )
        return OMX_ErrorInvalidComponent;
    if( hComponent->field_E4 != 1 )
        return OMX_ErrorNone;
    if( pBuffer->bufheader.nFilledLen == 0 )
        return OMX_ErrorNone;

    buffer = (OMX_U32*)pBuffer->bufheader.pBuffer;

    for( int i = 0; i < 32; ++i )
    {
        pstruc = &hComponent->buffers[i];
        if( pstruc->pBuffer == &pBuffer->bufheader )
            break;
        if( i == 31 )
        {
            ALOGE("Could not find corresponding input port buffer index!");
            return OMX_ErrorUndefined;
        }
    }

    pComponentConfigStructure.nSize = sizeof(OMX_METADATAPARAM);
    pComponentConfigStructure.field_4 = 1;
    pComponentConfigStructure.field_8 = 0;

    error = pComponent->StandardComp.GetConfig(pComponent, OMX_IndexConfigMarvellStoreMetaData, &pComponentConfigStructure);
    if( error != OMX_ErrorNone )
    {
        ALOGE("%s: GetConfig OMX_IndexConfigMarvellStoreMetaData failed, error = 0x%x", hComponent->ComponentName, error);
        return error;
    }

    if( buffer[0] == 1 )
    {
        private_handle_t *gcHandle = private_handle_t::dynamicCast((native_handle_t*)buffer[1]);
        gcFormat = gcHandle->format;
        if( gcFormat == HAL_PIXEL_FORMAT_YCbCr_420_P || gcFormat == HAL_PIXEL_FORMAT_YCbCr_420_SP_MRVL )
        {
            pBuffer->bufheader.pBuffer = (OMX_U8*)gcHandle->base;
            pBuffer->bufheader.nAllocLen = gcHandle->size;
            pBuffer->bufheader.nFilledLen = gcHandle->size;
            pBuffer->bufheader.nOffset = gcHandle->offset;
            err = mvmem_get_dma_addr(gcHandle->master, (int*)&dmaAddr);
            if( err < 0 )
            {
                ALOGE("failed to get VPUIO address through mvmem, return error:%d", err);
                return OMX_ErrorHardware;
            }
            if( !strncmp((char*)hComponent->ComponentName, "OMX.MARVELL.VIDEO.", 18) )
            {
                pBuffer->nPhyAddr = dmaAddr;
            }
            if( pComponentConfigStructure.format != 0x7F000789 )
                return OMX_ErrorNone;

            switch( gcFormat )
            {
                case HAL_PIXEL_FORMAT_YCbCr_420_P:
                    pComponentConfigStructure.format = OMX_COLOR_FormatYUV420Planar;
                    ALOGI("%s: In kMetadataBufferTypeGrallocSource usage, the input color format is OMX_COLOR_FormatYUV420Planar.", hComponent->ComponentName);
                    break;

                case HAL_PIXEL_FORMAT_YCbCr_420_SP_MRVL:
                    pComponentConfigStructure.format = OMX_COLOR_FormatYUV420SemiPlanar;
                    ALOGI("%s: In kMetadataBufferTypeGrallocSource usage, the input color format is OMX_COLOR_FormatYUV420SemiPlanar.", hComponent->ComponentName);
                    break;

                default:
                    ALOGE("%s: Unsupported hal pixel format: %d", hComponent->ComponentName, gcFormat);
                    return OMX_ErrorNotImplemented;
            }

            error = pComponent->StandardComp.SetConfig(pComponent, OMX_IndexConfigMarvellStoreMetaData, &pComponentConfigStructure);
            if( error != OMX_ErrorNone )
                ALOGE("%s, SetConfig OMX_IndexConfigMarvellStoreMetaData failed, error = 0x%x", hComponent->ComponentName, error);

            return error;
        }

        if( gcFormat == HAL_PIXEL_FORMAT_RGBA_8888 ||
            gcFormat == HAL_PIXEL_FORMAT_RGBX_8888 ||
            gcFormat == HAL_PIXEL_FORMAT_BGRA_8888 )
        {
            if( hComponent->context == NULL && hComponent->field_EC == 0 )
            {
                memset(&contextData, 0, sizeof(GCU_CONTEXT_DATA));
                gcuInitialize(&contextData);

                memset(&contextData2, 0, sizeof(GCU_CONTEXT_DATA));
                hComponent->context = gcuCreateContext(&contextData2);

                if( hComponent->context == NULL )
                {
                    ALOGE("GCU csc: create gcu context failed.");
                    return OMX_ErrorHardware;
                }

                gcuString = gcuGetString(GCU_RENDERER);
                if( gcuString && strstr(gcuString, "GC420") )
                {
                    hComponent->field_EC = 0;
                    ALOGI("Input buffer format %d, will use GCU HW CSC for GC420 platform.");
                }
                else
                {
                    hComponent->field_EC = 1;
                    ALOGI("Input buffer format %d, will use SOFTWARE CSC for NONE GC420 platform.");
                }
            }
            if( pComponentConfigStructure.format == 0x7F000789 )
            {
                if( !strcmp((char*)hComponent->ComponentName, "OMX.MARVELL.VIDEO.HW.HANTROENCODER") )
                {
                    if( hComponent->field_EC )
                    {
                        pComponentConfigStructure.format = OMX_COLOR_FormatYUV420SemiPlanar;
                        ALOGE("%s: In kMetadataBufferTypeGrallocSource usage, the input color format is OMX_COLOR_FormatYUV420SemiPlanar (after CSC).", hComponent->ComponentName);
                    }
                    else
                    {
                        pComponentConfigStructure.format = OMX_COLOR_FormatCbYCrY;
                        ALOGE("%s: In kMetadataBufferTypeGrallocSource usage, the input color format is OMX_COLOR_FormatCbYCrY (after CSC).", hComponent->ComponentName);
                    }
                }
                else
                {
                    pComponentConfigStructure.format = OMX_COLOR_FormatYUV420SemiPlanar;
                    ALOGE("%s: In kMetadataBufferTypeGrallocSource usage, the input color format is OMX_COLOR_FormatYUV420SemiPlanar (after CSC).", hComponent->ComponentName);
                }

                error = pComponent->StandardComp.SetConfig(pComponent, OMX_IndexConfigMarvellStoreMetaData, &pComponentConfigStructure);
                if( error != OMX_ErrorNone )
                {
                    ALOGE("%s, SetConfig OMX_IndexConfigMarvellStoreMetaData failed, error = 0x%x", hComponent->ComponentName);
                    return error;
                }

                if( pstruc->memHeap.get() == NULL )
                {
                    if( pComponentConfigStructure.format == OMX_COLOR_FormatCbYCrY )
                        size = 2 * _ALIGN(pComponentConfigStructure.width, 16) * _ALIGN(pComponentConfigStructure.height, 16);
                    else if( pComponentConfigStructure.format == OMX_COLOR_FormatYUV420SemiPlanar )
                        size = 3 * _ALIGN(pComponentConfigStructure.width, 16) * _ALIGN(pComponentConfigStructure.height, 16) / 2;
                    else
                    {
                        ALOGE("%s: Unsupported target csc color format: %d", hComponent->ComponentName, pComponentConfigStructure.format);
                        return OMX_ErrorNotImplemented;
                    }
                }
                android::sp<android::MemoryHeapBase> memHeap = new android::MemoryHeapBase("/dev/ion", size, android::MemoryHeapBase::PHYSICALLY_CONTIGUOUS|android::MemoryHeapBase::NO_CACHING);



            }
        }

    }
    if( buffer[0] )
    {
       ALOGE("Unsupported StoreMetaDataInVideoBuffers usage type: %d", buffer[0]);
       return OMX_ErrorNotImplemented;
    }
}

static OMX_ERRORTYPE IppOMXWrapper_GetParameter(
        OMX_IN  OMX_HANDLETYPE hComponent,
        OMX_IN  OMX_INDEXTYPE nParamIndex,
        OMX_INOUT OMX_PTR pComponentParameterStructure){
   if (hComponent == NULL){
        return OMX_ErrorInvalidComponent;
   }
   IppOmxCompomentWrapper_t *pComponent = IPPOMX_PCOMPONENT(hComponent);

   return pComponent->StandardComp.GetParameter(pComponent, nParamIndex, pComponentParameterStructure);
}


/** refer to OMX_SetParameter in OMX_core.h or the OMX IL
    specification for details on the SetParameter method.
 */
static OMX_ERRORTYPE IppOMXWrapper_SetParameter(
        OMX_IN  OMX_HANDLETYPE hComponent,
        OMX_IN  OMX_INDEXTYPE nIndex,
        OMX_IN  OMX_PTR pComponentParameterStructure){
    if (hComponent == NULL){
        return OMX_ErrorInvalidComponent;
    }
    OMX_ERRORTYPE res;
    IppOmxCompomentWrapper_t *component = IPPOMX_COMPONENT(hComponent);
    IppOmxCompomentWrapper_t *pComponent = IPPOMX_PCOMPONENT(hComponent);
    unsigned char *name = component->ComponentName;
    int32_t *InParam = (int32_t*)pComponentParameterStructure;
    int32_t params[4];

    if( nIndex != OMX_IndexParamMarvellStoreMetaInOutputBuff )
        return pComponent->StandardComp.SetParameter(pComponent, nIndex, pComponentParameterStructure);

    if( InParam[2] )
    {
        ALOGI("%s: currently we do not support StoreMetaDataInOutputBuffers usage.", name);
        return OMX_ErrorNotImplemented;
    }

    component->field_E4 = InParam[3];

    params[0] = InParam[0];
    params[1] = InParam[1];
    params[2] = InParam[2];
    params[3] = InParam[3];

    res = pComponent->StandardComp.SetParameter(pComponent, (OMX_INDEXTYPE)OMX_IndexParamMarvellStoreMetaInOutputBuff, params);
    if( res != OMX_ErrorNone )
    {
        ALOGE("%s: SetParameter OMX_IndexParamMarvellStoreMetaData failed, error = 0x%x", name, res);
    }
    return res;
}


/** refer to OMX_GetConfig in OMX_core.h or the OMX IL
    specification for details on the GetConfig method.
 */
static OMX_ERRORTYPE IppOMXWrapper_GetConfig(
        OMX_IN  OMX_HANDLETYPE hComponent,
        OMX_IN  OMX_INDEXTYPE nIndex,
        OMX_INOUT OMX_PTR pComponentConfigStructure){
   if (hComponent == NULL){
        return OMX_ErrorInvalidComponent;
   }
   IppOmxCompomentWrapper_t *pComponent = IPPOMX_PCOMPONENT(hComponent);

   return pComponent->StandardComp.GetConfig(pComponent, nIndex, pComponentConfigStructure);
}


/** refer to OMX_SetConfig in OMX_core.h or the OMX IL
    specification for details on the SetConfig method.
 */
static OMX_ERRORTYPE IppOMXWrapper_SetConfig(
        OMX_IN  OMX_HANDLETYPE hComponent,
        OMX_IN  OMX_INDEXTYPE nIndex,
        OMX_IN  OMX_PTR pComponentConfigStructure){
   if (hComponent == NULL){
        return OMX_ErrorInvalidComponent;
   }
   IppOmxCompomentWrapper_t *pComponent = IPPOMX_PCOMPONENT(hComponent);

   return pComponent->StandardComp.SetConfig(pComponent, nIndex, pComponentConfigStructure);
}

/** refer to OMX_GetExtensionIndex in OMX_core.h or the OMX IL
    specification for details on the GetExtensionIndex method.
 */
static OMX_ERRORTYPE IppOMXWrapper_GetExtensionIndex(
        OMX_IN  OMX_HANDLETYPE hComponent,
        OMX_IN  OMX_STRING cParameterName,
        OMX_OUT OMX_INDEXTYPE* pIndexType){
   if (hComponent == NULL){
        return OMX_ErrorInvalidComponent;
   }
   IppOmxCompomentWrapper_t *pComponent = IPPOMX_PCOMPONENT(hComponent);

   return pComponent->StandardComp.GetExtensionIndex(pComponent, cParameterName, pIndexType);
}

static OMX_ERRORTYPE IppOMXWrapper_UseBuffer(
        OMX_IN OMX_HANDLETYPE hComponent,
        OMX_INOUT OMX_BUFFERHEADERTYPE** ppBufferHdr,
        OMX_IN OMX_U32 nPortIndex,
        OMX_IN OMX_PTR pAppPrivate,
        OMX_IN OMX_U32 nSizeBytes,
        OMX_IN OMX_U8* pBuffer){
    if (hComponent == NULL){
        return OMX_ErrorInvalidComponent;
    }
    IppOmxCompomentWrapper_t *component = IPPOMX_COMPONENT(hComponent);
    IppOmxCompomentWrapper_t *pComponent = IPPOMX_PCOMPONENT(hComponent);
    OMX_ERRORTYPE error;

    error = pComponent->StandardComp.UseBuffer(pComponent, ppBufferHdr, nPortIndex, pAppPrivate, nSizeBytes, pBuffer);

    if( error == OMX_ErrorNone && component->field_E4 == 1 && !nPortIndex )
    {
        int numBuffer = component->numBuffers;
        if( numBuffer < 32 )
        {
            struc_1 *buffer = &component->buffers[numBuffer];
            buffer->pBuffer = *ppBufferHdr;
            memcpy(&buffer->bufferHeader, *ppBufferHdr, 0x50);
            ++component->numBuffers;
            error = OMX_ErrorNone;
        }
        else
        {
            ALOGE("Input port buffer count exceeds max count %d, please check!", 32);
            error = OMX_ErrorInsufficientResources;
        }
    }

    return error;
}
/** refer to OMX_AllocateBuffer in OMX_core.h or the OMX IL
    specification for details on the AllocateBuffer method.
    @ingroup buf
 */
static OMX_ERRORTYPE IppOMXWrapper_AllocateBuffer(
        OMX_IN OMX_HANDLETYPE hComponent,
        OMX_INOUT OMX_BUFFERHEADERTYPE** ppBuffer,
        OMX_IN OMX_U32 nPortIndex,
        OMX_IN OMX_PTR pAppPrivate,
        OMX_IN OMX_U32 nSizeBytes){
    OMX_ERRORTYPE error = OMX_ErrorNone;

    if (hComponent == NULL){
         return OMX_ErrorInvalidComponent;
    }
    IppOmxCompomentWrapper_t *component = IPPOMX_COMPONENT(hComponent);
    IppOmxCompomentWrapper_t *pComponent = IPPOMX_PCOMPONENT(hComponent);

    error = pComponent->StandardComp.AllocateBuffer(pComponent, ppBuffer, nPortIndex, pAppPrivate, nSizeBytes);
    if( error == OMX_ErrorNone && component->field_E4 == 1 && !nPortIndex )
    {
        int numBuffers = component->numBuffers;
        if( numBuffers < 32 )
        {
            struc_1 *pstruc = &component->buffers[numBuffers];
            pstruc->pBuffer = *ppBuffer;
            memcpy(&pstruc->bufferHeader, *ppBuffer, 0x50);
            ++component->numBuffers;
            error = OMX_ErrorNone;
        }
        else
        {
            ALOGE("Input port buffer count exceeds max count %d, please check!", 32);
            error = OMX_ErrorInsufficientResources;
        }
    }

    return error;
}

/** refer to OMX_FreeBuffer in OMX_core.h or the OMX IL
    specification for details on the FreeBuffer method.
    @ingroup buf
 */
static OMX_ERRORTYPE IppOMXWrapper_FreeBuffer(
        OMX_IN  OMX_HANDLETYPE hComponent,
        OMX_IN  OMX_U32 nPortIndex,
        OMX_IN  OMX_BUFFERHEADERTYPE* pBuffer){
    OMX_ERRORTYPE error = OMX_ErrorNone;
    if (hComponent == NULL){
        return OMX_ErrorInvalidComponent;
    }
    IppOmxCompomentWrapper_t *component = IPPOMX_COMPONENT(hComponent);
    IppOmxCompomentWrapper_t *pComponent = IPPOMX_PCOMPONENT(hComponent);
    struc_1 *pstruc;

    error = pComponent->StandardComp.FreeBuffer(pComponent, nPortIndex, pBuffer);
    if( error != OMX_ErrorNone )
    {
        ALOGE("%s: OMX_FreeBuffer Failed: port %lu , ptr: %p", component->ComponentName, nPortIndex, pBuffer->pBuffer);
        return error;
    }

    if( component->field_E4 != 1 || nPortIndex )
        return error;

    for( int i = 0; i < 32; ++i )
    {
        pstruc = &component->buffers[i];
        if( pstruc->pBuffer == pBuffer )
            break;
        if( i == 31 )
        {
            ALOGE("Could not find backup input port buffer header!");
            return OMX_ErrorUndefined;
        }
    }

    pstruc->pBuffer = NULL;
    if( pstruc->surface )
    {
        if( component->context == NULL )
        {
            ALOGE("Invalid GCU context.");
            return OMX_ErrorHardware;
        }
        _gcuDestroyBuffer(component->context, pstruc->surface);
        pstruc->surface = NULL;
    }

    if( pstruc->memHeap.get() )
    {
        pstruc->memHeap = NULL;
        pstruc->field_60 = 0;
        pstruc->field_64 = 0;
        pstruc->field_6C = 0;
        pstruc->surface = NULL;
    }
    memset(&pstruc->bufferHeader, 0, sizeof(OMX_BUFFERHEADERTYPE));
    error = OMX_ErrorNone;
    --component->numBuffers;

    return error;
}

/** refer to OMX_EmptyThisBuffer in OMX_core.h or the OMX IL
    specification for details on the EmptyThisBuffer method.
    @ingroup buf
 */
static OMX_ERRORTYPE IppOMXWrapper_EmptyThisBuffer(
        OMX_IN  OMX_HANDLETYPE hComponent,
        OMX_IN  OMX_BUFFERHEADERTYPE* pBuffer){

    OMX_ERRORTYPE error = OMX_ErrorNone;

    if (hComponent == NULL){
        return OMX_ErrorInvalidComponent;
    }
    IppOmxCompomentWrapper_t *component = IPPOMX_COMPONENT(hComponent);
    IppOmxCompomentWrapper_t *pComponent = IPPOMX_PCOMPONENT(hComponent);

    if( component->field_E4 == 1 )
    {
        if( pBuffer->nFilledLen )
        {
            error = storeMetaDataInBufferHandling(component, (OMX_BUFFERHEADERTYPE_IPPEXT*)pBuffer);
            if( error != OMX_ErrorNone )
            {
                ALOGE("%s, storeMetaDataInBufferHandling() failed, error = 0x%x", component->ComponentName, error);
                return error;
            }
        }
    }

    if( pBuffer->pInputPortPrivate )
    {
        android::IMemory *mem = (android::IMemory*)(pBuffer->pInputPortPrivate);
        android::sp<android::IMemoryHeap> heap = mem->getMemory();
        int fd = heap->getHeapID();
        int note;
        int err;

        if( strncmp((const char*)component->ComponentName, "OMX.MARVELL.VIDEO.HW", 20) )
            note = ION_HEAP_TYPE_DMA;
        else
            note = ION_HEAP_TYPE_SYSTEM_CONTIG;

        err = mvmem_set_usage(fd, note);
        if( err < 0 )
        {
            ALOGE("failed to set buffer usage through mvmem, return error:%d", err);
            return OMX_ErrorHardware;
        }
    }

    if( !strcmp((const char*)component->ComponentName, "OMX.MARVELL.VIDEO.HW.CODA7542ENCODER")
     || !strcmp((const char*)component->ComponentName, "OMX.MARVELL.VIDEO.HW.HANTROENCODER") )
    {
        android::IMemory *mem = (android::IMemory*)(pBuffer->pInputPortPrivate);
        if( mem )
        {
            android::sp<android::IMemoryHeap> heap = mem->getMemory();
            int fd = heap->getHeapID();
            int err;
            uint32_t dmaaddr;
            void *va_base;
            OMX_S32 offset;
            err = mvmem_get_dma_addr(fd, (int*)&dmaaddr);
            if( err >= 0 )
            {
                ((OMX_BUFFERHEADERTYPE_IPPEXT*)pBuffer)->nPhyAddr = offset + dmaaddr;
            }
            else
            {
                ((OMX_BUFFERHEADERTYPE_IPPEXT*)pBuffer)->nPhyAddr = 0;
                ALOGE("failed to get physical address through mvmem, return error:%d", err);
                ALOGE("The Physical address for HW encoder is illegal NULL");
            }
        }
    }

    return pComponent->StandardComp.EmptyThisBuffer(pComponent, pBuffer);
}

/** refer to OMX_FillThisBuffer in OMX_core.h or the OMX IL
    specification for details on the FillThisBuffer method.
    @ingroup buf
 */
static OMX_ERRORTYPE IppOMXWrapper_FillThisBuffer(
        OMX_IN  OMX_HANDLETYPE hComponent,
        OMX_IN  OMX_BUFFERHEADERTYPE* pBuffer){
    if (hComponent == NULL){
        return OMX_ErrorInvalidComponent;
    }

    IppOmxCompomentWrapper_t *pComponent = IPPOMX_PCOMPONENT(hComponent);
    return pComponent->StandardComp.FillThisBuffer(pComponent, pBuffer);
}

static OMX_ERRORTYPE IppOMXWrapper_GetComponentVersion(
        OMX_IN  OMX_HANDLETYPE hComponent,
        OMX_OUT OMX_STRING pComponentName,
        OMX_OUT OMX_VERSIONTYPE* pComponentVersion,
        OMX_OUT OMX_VERSIONTYPE* pSpecVersion,
        OMX_OUT OMX_UUIDTYPE* pComponentUUID){
    if (hComponent == NULL){
        return OMX_ErrorInvalidComponent;
    }
    IppOmxCompomentWrapper_t *pComponent = IPPOMX_PCOMPONENT(hComponent);

    return pComponent->StandardComp.GetComponentVersion(pComponent, pComponentName,pComponentVersion, pSpecVersion, pComponentUUID);
}

/** refer to OMX_SendCommand in OMX_core.h or the OMX IL
    specification for details on the SendCommand method.
 */
static OMX_ERRORTYPE IppOMXWrapper_SendCommand(
        OMX_IN  OMX_HANDLETYPE hComponent,
        OMX_IN  OMX_COMMANDTYPE Cmd,
        OMX_IN  OMX_U32 nParam1,
        OMX_IN  OMX_PTR pCmdData){
    if (hComponent == NULL){
        return OMX_ErrorInvalidComponent;
    }
    IppOmxCompomentWrapper_t *component = IPPOMX_COMPONENT(hComponent);
    IppOmxCompomentWrapper_t *pComponent = IPPOMX_PCOMPONENT(hComponent);

    ALOGD("%s: OMX_SendCommand: cmd: %d, nParam1: %lu ", component->ComponentName, Cmd, nParam1);
    return pComponent->StandardComp.SendCommand(pComponent, Cmd, nParam1, pCmdData);
}

/** refer to OMX_GetState in OMX_core.h or the OMX IL
    specification for details on the GetState method.
 */
static OMX_ERRORTYPE IppOMXWrapper_GetState(
        OMX_IN  OMX_HANDLETYPE hComponent,
        OMX_OUT OMX_STATETYPE* pState){
    if (hComponent == NULL){
        return OMX_ErrorInvalidComponent;
    }
    IppOmxCompomentWrapper_t *pComponent = IPPOMX_PCOMPONENT(hComponent);

    return pComponent->StandardComp.GetState(pComponent, pState);
}

/** @ingroup buf */
static OMX_ERRORTYPE IppOMXWrapper_UseEGLImage(
        OMX_IN OMX_HANDLETYPE hComponent,
        OMX_INOUT OMX_BUFFERHEADERTYPE** ppBufferHdr,
        OMX_IN OMX_U32 nPortIndex,
        OMX_IN OMX_PTR pAppPrivate,
        OMX_IN void* eglImage){
    if (hComponent == NULL){
        return OMX_ErrorInvalidComponent;
    }
    IppOmxCompomentWrapper_t *pComponent = IPPOMX_PCOMPONENT(hComponent);

    return pComponent->StandardComp.UseEGLImage(pComponent, ppBufferHdr,nPortIndex,pAppPrivate,eglImage);
}

static     OMX_ERRORTYPE IppOMXWrapper_ComponentRoleEnum(
        OMX_IN OMX_HANDLETYPE hComponent,
        OMX_OUT OMX_U8 *cRole,
        OMX_IN OMX_U32 nIndex){
   if (hComponent == NULL){
        return OMX_ErrorInvalidComponent;
   }
   IppOmxCompomentWrapper_t *pComponent = IPPOMX_PCOMPONENT(hComponent);


   return pComponent->StandardComp.ComponentRoleEnum(pComponent, cRole, nIndex);
}

static OMX_ERRORTYPE IppOMXWrapper_EventHandler(
        OMX_IN OMX_HANDLETYPE hComponent,
        OMX_IN OMX_PTR pAppData,
        OMX_IN OMX_EVENTTYPE eEvent,
        OMX_IN OMX_U32 nData1,
        OMX_IN OMX_U32 nData2,
        OMX_IN OMX_PTR pEventData){
   IppOmxCompomentWrapper_t *hWrapperHandle = IPPOMX_PAPPLICATION(hComponent);
   OMX_ERRORTYPE error;
   if (hWrapperHandle == NULL){
        return OMX_ErrorInvalidComponent;
   }

   error = hWrapperHandle->InternalCallBack.EventHandler(hWrapperHandle, pAppData, eEvent, nData1, nData2, pEventData);
   ALOGD("%s: Event: %d, nData1: %lu, nData2: %lu",hWrapperHandle->ComponentName, eEvent, nData1, nData2);

   return error;
}


static OMX_ERRORTYPE IppOMXWrapper_EmptyBufferDone(
        OMX_IN OMX_HANDLETYPE hComponent,
        OMX_IN OMX_PTR pAppData,
        OMX_IN OMX_BUFFERHEADERTYPE* pBuffer){
   IppOmxCompomentWrapper_t *hWrapperHandle = IPPOMX_PAPPLICATION(hComponent);
   OMX_ERRORTYPE error;

    if (hWrapperHandle == NULL){
        return OMX_ErrorInvalidComponent;
    }

    if ( hWrapperHandle->field_E4 == 1 )
    {
        struc_1 *buffers;
        for( int i = 0; i < 32; ++i )
        {
            buffers = &hWrapperHandle->buffers[i];
            if ( hWrapperHandle->buffers[i].pBuffer == pBuffer )
                break;
            if ( i == 31 )
            {
                ALOGE("Could not find backup input port buffer header!.\n");
                return OMX_ErrorUndefined;
            }
        }
        pBuffer->pBuffer   = buffers->bufferHeader.pBuffer;
        pBuffer->nAllocLen = buffers->bufferHeader.nAllocLen;
        pBuffer->nOffset   = buffers->bufferHeader.nOffset;
    }

   return hWrapperHandle->InternalCallBack.EmptyBufferDone(hWrapperHandle, pAppData, pBuffer);
}


static OMX_ERRORTYPE IppOMXWrapper_FillBufferDone(
        OMX_OUT OMX_HANDLETYPE hComponent,
        OMX_OUT OMX_PTR pAppData,
        OMX_OUT OMX_BUFFERHEADERTYPE* pBuffer){
   IppOmxCompomentWrapper_t *hWrapperHandle = IPPOMX_PAPPLICATION(hComponent);
   OMX_ERRORTYPE error;
    if (hWrapperHandle == NULL){
        return OMX_ErrorInvalidComponent;
    }

    return hWrapperHandle->InternalCallBack.FillBufferDone(hWrapperHandle, pAppData, pBuffer);
}


static OMX_ERRORTYPE _OMX_MasterInit()
{
    return OMX_Init();
}

/***************************************************************************
// Name:             OMX_Deinit
// Description:      Deinitialize the OMX core. The last call made into OMX.
// Call Type:         Blocking Call. Must respond within 20 msec
//
// Input Arguments:
//                        None
// Output Arguments:
//                        None
// Returns:
//                        OMX_ErrorNone:    Function Successfully Executed
//                        OMX_ErrorVersionMismatch: OMX core version mismatch
*****************************************************************************/
static OMX_ERRORTYPE _OMX_MasterDeinit()
{
    return OMX_Deinit();
}

/***************************************************************************
// Name:             OMX_GetHandle
// Description:      Create a component handle.
// Call Type:         Blocking Call. Must respond within 20 msec
//
// Input Arguments:
//        cComponentName    - Pointer to a null terminated string with the component name with
//                          following format: "OMX.<vendor_name>.<vendor_specified_convention>"
//        pAppData        - Pointer to an application defined value so that the
//                          application can identify the source of the callback
//        pCallBacks        - pointer to a OMX_CALLBACKTYPE structure that will be
//                          passed to the component to initialize it with
// Output Arguments:
//        pHandle            - Pointer to an OMX_HANDLETYPE pointer to be filled
// Returns:
//            OMX_ErrorNone:    Function Successfully Executed
//            OMX_ErrorInsufficientResources:    Allocate OMX_COMPONENTTYPE structure failed
//            OMX_ErrorComponentNotFound: OMX Core didn't support this type of component
//            OMX_ErrorUndefined: OMX_ComponentInit failed
//            OMX_ErrorInvalidComponentName: The component name is not valid
//            OMX_ErrorInvalidComponent: The component specified didn't have a "OMX_ComponentInit" entry
//            OMX_ErrorBadParameter: Callback function entries error
//            OMX_ErrorVersionMismatch: OMX component version mismatch
*****************************************************************************/

static OMX_ERRORTYPE _OMX_MasterGetHandle(
    OMX_OUT OMX_HANDLETYPE* pHandle,
    OMX_IN  OMX_STRING cComponentName,
    OMX_IN  OMX_PTR pAppData,
    OMX_IN  OMX_CALLBACKTYPE* pCallBacks)
{
    IppOmxCompomentWrapper_t *pWrapperHandle=NULL;
    OMX_HANDLETYPE pOmxInternalHandle = NULL;
    OMX_ERRORTYPE error=OMX_ErrorNone;
    OMX_CALLBACKTYPE WrapperCallBack;

    WrapperCallBack.EmptyBufferDone = IppOMXWrapper_EmptyBufferDone;
    WrapperCallBack.FillBufferDone  = IppOMXWrapper_FillBufferDone;
    WrapperCallBack.EventHandler    = IppOMXWrapper_EventHandler;

    pWrapperHandle = (IppOmxCompomentWrapper_t*)malloc(sizeof(IppOmxCompomentWrapper_t));
    if (!pWrapperHandle) {
        return OMX_ErrorInsufficientResources;
    }

    memset(pWrapperHandle, 0, sizeof(IppOmxCompomentWrapper_t));
    error = OMX_GetHandle(&pOmxInternalHandle, cComponentName, pAppData, &WrapperCallBack);
    if (error == OMX_ErrorNone){
        *pHandle = (OMX_HANDLETYPE)pWrapperHandle;
        /*override function*/
        pWrapperHandle->StandardComp.GetComponentVersion = IppOMXWrapper_GetComponentVersion;
        pWrapperHandle->StandardComp.SendCommand         = IppOMXWrapper_SendCommand;
        pWrapperHandle->StandardComp.GetParameter        = IppOMXWrapper_GetParameter;
        pWrapperHandle->StandardComp.SetParameter        = IppOMXWrapper_SetParameter;
        pWrapperHandle->StandardComp.GetConfig           = IppOMXWrapper_GetConfig;
        pWrapperHandle->StandardComp.SetConfig           = IppOMXWrapper_SetConfig;
        pWrapperHandle->StandardComp.GetExtensionIndex   = IppOMXWrapper_GetExtensionIndex;
        pWrapperHandle->StandardComp.GetState            = IppOMXWrapper_GetState;
        pWrapperHandle->StandardComp.UseBuffer           = IppOMXWrapper_UseBuffer;
        pWrapperHandle->StandardComp.AllocateBuffer      = IppOMXWrapper_AllocateBuffer;
        pWrapperHandle->StandardComp.FreeBuffer          = IppOMXWrapper_FreeBuffer;
        pWrapperHandle->StandardComp.EmptyThisBuffer     = IppOMXWrapper_EmptyThisBuffer;
        pWrapperHandle->StandardComp.FillThisBuffer      = IppOMXWrapper_FillThisBuffer;
        pWrapperHandle->StandardComp.UseEGLImage         = IppOMXWrapper_UseEGLImage;
        pWrapperHandle->StandardComp.ComponentRoleEnum   = IppOMXWrapper_ComponentRoleEnum;

        pWrapperHandle->InternalCallBack.EmptyBufferDone  = pCallBacks->EmptyBufferDone;
        pWrapperHandle->InternalCallBack.FillBufferDone   = pCallBacks->FillBufferDone;
        pWrapperHandle->InternalCallBack.EventHandler     = pCallBacks->EventHandler;

        pWrapperHandle->field_E4 = 0;
        pWrapperHandle->context = NULL;
        pWrapperHandle->field_EC = 0;
        pWrapperHandle->numBuffers = 0;

        for( int i = 0; i < 32; ++i )
        {
            pWrapperHandle->buffers[i].pBuffer = NULL;
            pWrapperHandle->buffers[i].field_4 = 0;
            pWrapperHandle->buffers[i].memHeap = NULL;
            memset(&pWrapperHandle->buffers[i].bufferHeader, 0, sizeof(OMX_BUFFERHEADERTYPE));
            pWrapperHandle->buffers[i].field_5C = 0;
            pWrapperHandle->buffers[i].field_60 = 0;
            pWrapperHandle->buffers[i].field_64 = 0;
            pWrapperHandle->buffers[i].surface = NULL;
            pWrapperHandle->buffers[i].field_6C = 0;
        }

        ((OMX_COMPONENTTYPE*)pOmxInternalHandle)->pApplicationPrivate = pWrapperHandle;
        pWrapperHandle->StandardComp.pComponentPrivate = pOmxInternalHandle;
        strncpy((char*)pWrapperHandle->ComponentName, cComponentName, 128);

    if (!strcmp(cComponentName, "OMX.MARVELL.AUDIO.AACENCODER")) {
        /*config aac encoder to output specific data for stagefright-based camcorder*/
        OMX_AUDIO_PARAM_MARVELL_AACENC par;
        par.nSize         	    = sizeof(OMX_AUDIO_PARAM_MARVELL_AACENC);
        par.nVersion.nVersion   = 1;
        par.nPortIndex    	    = 1;
        par.bOutputSpecificData = OMX_TRUE;
        error = ((OMX_COMPONENTTYPE*)pOmxInternalHandle)->SetParameter(pOmxInternalHandle, (OMX_INDEXTYPE)OMX_IndexParamMarvellAACEnc, &par);
    }

    if (!strcmp(cComponentName, "OMX.MARVELL.VIDEO.VMETADECODER")) {
        OMX_VIDEO_PARAM_MARVELL_VMETADEC par;
        par.nSize = sizeof(OMX_VIDEO_PARAM_MARVELL_VMETADEC);
        par.nVersion.nVersion = 1;

        error = ((OMX_COMPONENTTYPE*)pOmxInternalHandle)->GetParameter(pOmxInternalHandle, (OMX_INDEXTYPE)OMX_IndexParamMarvellVmetaDec, &par);
        if(error){
            ALOGE("Get parameter OMX_IndexParamMarvellVmetaDec failed, error = 0x%x", error);
            return error;
        }

#ifdef ENABLE_VMETADEC_POWEROPT
        par.nAdvanAVSync |= ENABLE_POWEROPT;
#else
        par.nAdvanAVSync &= ~ENABLE_POWEROPT;
#endif

#ifdef ENABLE_VMETADEC_ADVANAVSYNC_1080P
        par.nAdvanAVSync |= ENABLE_ADVANAVSYNC_1080P;
#else
        par.nAdvanAVSync &= ~ENABLE_ADVANAVSYNC_1080P;
#endif

        error = OMX_SetParameter(pOmxInternalHandle, (OMX_INDEXTYPE)OMX_IndexParamMarvellVmetaDec, &par);
        if(error){
            ALOGE("Set parameter OMX_IndexParamMarvellVmetaDec failed, error = 0x%x", error);
            return error;
        }

        ALOGD("PLATFORM_SDK_VERSION = %d, ENABLE_ADVANAVSYNC_1080P = 0x%lx, ENABLE_POWEROPT = 0x%lx",
              PLATFORM_SDK_VERSION, par.nAdvanAVSync&ENABLE_ADVANAVSYNC_1080P, par.nAdvanAVSync&ENABLE_POWEROPT);
    }

        MARVELL_LOG("OMX_GetHandle succeeded and exiting %s", pWrapperHandle->ComponentName);
        return error;
    }else{
        ALOGE("OMX_GetHandle failed %s (ret=0x%x)", cComponentName, error);
        free(pWrapperHandle);
        *pHandle = NULL;
        return error;
    }
}

/***************************************************************************
// Name:             OMX_FreeHandle
// Description:      Free a component handle.
// Call Type:         Blocking Call. Must respond within 20 msec
//
// Input Arguments:
//        hComponent        - Handle of the component to be accessed
// Output Arguments:
//                        None
// Returns:
//            OMX_ErrorNone:    Function Successfully Executed
//            OMX_ErrorIncorrectStateOperation: Component is NOT in the loaded or invalid state
//            OMX_ErrorUndefined: Free resources failed
//            OMX_ErrorInvalidComponent: The component specified didn't have a "OMX_ComponentDeinit" entry
//            OMX_ErrorVersionMismatch: OMX component version mismatch
*****************************************************************************/
static OMX_ERRORTYPE _OMX_MasterFreeHandle(
    OMX_IN  OMX_HANDLETYPE hComponent)
{
    IppOmxCompomentWrapper_t *hWrapperHandle = (IppOmxCompomentWrapper_t*)hComponent;
    OMX_ERRORTYPE error = OMX_ErrorNone;

    if( hWrapperHandle->context)
    {
        gcuDestroyContext(&hWrapperHandle->context);
        gcuTerminate();
        hWrapperHandle->context = 0;
    }

    error = OMX_FreeHandle((OMX_HANDLETYPE)hWrapperHandle->StandardComp.pComponentPrivate);

    free(hWrapperHandle);

    return error;

}

/***************************************************************************
// Name:             OMX_ComponentNameEnum
// Description:      Enumerate through all the names of recognized valid
//                     components in the system
// Call Type:         Blocking Call.
//
// Input Arguments:
//        nNameLength        - Number of characters in the cComponentName string
//        nIndex            - Number containing the enumeration index for the component
// Output Arguments:
//        cComponentName    - Pointer to a null terminated string with the component name
// Returns:
//            OMX_ErrorNone:    Function Successfully Executed
//            OMX_ErrorNoMore:  nIndex exceeds the number of components in the system minus 1
//            OMX_ErrorUndefined: Fill in cComponentName failed
*****************************************************************************/
static OMX_ERRORTYPE _OMX_MasterComponentNameEnum(
    OMX_OUT OMX_STRING cComponentName,
    OMX_IN  OMX_U32 nNameLength,
    OMX_IN  OMX_U32 nIndex)
{
    return OMX_ComponentNameEnum(cComponentName, nNameLength, nIndex);
}

/***************************************************************************
// Name:             OMX_SetupTunnel
// Description:      Setup the specified path between the two components
// Call Type:         Blocking Call. should return within 20 ms
//
// Input Arguments:
//        hOutput            - Handle of the component to be accessed.  It holds the
//                          port to be specified in the nPortOutput parameter.
//                          It is required that hOutput be the source for the data
//        nPortOutput        - Indicate the source port on component to be used in the tunnel
//        hInput            - Handle of he component to setup the tunnel with.  It holds
//                          the port to be specified in the nPortInput parameter.
//                          It is required that hInput be the destination for the data
//        nPortInput        - Indicate the destination port on component to be used in the tunnel
// Output Arguments:
//                        None
// Returns:
//        OMX_ErrorNone:    Function Successfully Executed
//        OMX_ErrorBadParameter: Input parameter is not valid
//        OMX_ErrorInvalidState: Component is in invalid state
//        OMX_ErrorPortsNotCompatible: Ports being connected are not compatible
//        OMX_ErrorVersionMismatch: The component version didn't match
*****************************************************************************/
static OMX_ERRORTYPE _OMX_MasterSetupTunnel(
    OMX_IN  OMX_HANDLETYPE hOutput,
    OMX_IN  OMX_U32 nPortOutput,
    OMX_IN  OMX_HANDLETYPE hInput,
    OMX_IN  OMX_U32 nPortInput)
{
    OMX_COMPONENTTYPE *pWrapperOutput = (OMX_COMPONENTTYPE*)hOutput;
    OMX_COMPONENTTYPE *pWrapperInput = (OMX_COMPONENTTYPE*)hInput;

    return OMX_SetupTunnel(pWrapperOutput->pComponentPrivate, nPortOutput, pWrapperInput->pComponentPrivate, nPortInput);

}


/***************************************************************************
// Name:             OMX_GetRolesOfcomponent
// Description:      query all the roles fulfilled by a given component
// Call Type:         Blocking Call. should return within 5 ms
//
// Input Arguments:
//        compName        - Name of the component being queried about
// InOut Arguments:
//        pNumRoles:        - On input it bounds the size of the input structure.
//                        - On output it specifies how many roles were retrieved.
//        roles:            - List of the names of all standard components implemented on the
//                          specified physical component name.
//                          If this pointer is NULL this function populates the pNumRoles
//                          field with the number of roles the component supports and
//                          ignores the roles field.
// Output Arguments:
//                        None
// Returns:
//        OMX_ErrorNone:    Function Successfully Executed
*****************************************************************************/
static OMX_ERRORTYPE _OMX_MasterGetRolesOfComponent (
    OMX_IN      OMX_STRING compName,
    OMX_INOUT   OMX_U32 *pNumRoles,
    OMX_OUT     OMX_U8 **roles)
{

    return OMX_GetRolesOfComponent(compName, pNumRoles, roles);
}

static OMX_ERRORTYPE _OMX_MasterGetComponentsOfRole (
    OMX_IN      OMX_STRING role,
    OMX_INOUT   OMX_U32 *pNumComps,
    OMX_INOUT   OMX_U8  **compNames)
{
    return OMX_GetComponentsOfRole(role, pNumComps, compNames);
}

static OMX_ERRORTYPE _OMX_MasterGetContentPipe( OMX_OUT OMX_HANDLETYPE *hPipe, OMX_IN OMX_STRING szURI )
{
    return OMX_GetContentPipe(hPipe, szURI);
}

namespace android {
OMXMRVLCodecsPlugin * createOMXPlugin(){
    return new OMXMRVLCodecsPlugin;
}

OMXMRVLCodecsPlugin::OMXMRVLCodecsPlugin() {
    _OMX_MasterInit();
}

OMXMRVLCodecsPlugin::~OMXMRVLCodecsPlugin() {
    _OMX_MasterDeinit();
}

OMX_ERRORTYPE OMXMRVLCodecsPlugin::makeComponentInstance(
        const char *name,
        const OMX_CALLBACKTYPE *callbacks,
        OMX_PTR appData,
        OMX_COMPONENTTYPE **component) {
    return _OMX_MasterGetHandle(
            reinterpret_cast<OMX_HANDLETYPE *>(component),
            const_cast<char *>(name),
            appData,
            const_cast<OMX_CALLBACKTYPE *>(callbacks));
}

OMX_ERRORTYPE OMXMRVLCodecsPlugin::destroyComponentInstance(
        OMX_COMPONENTTYPE *component) {
    return _OMX_MasterFreeHandle(component);
}

OMX_ERRORTYPE OMXMRVLCodecsPlugin::enumerateComponents(
        OMX_STRING name,
        size_t size,
        OMX_U32 index) {
    return _OMX_MasterComponentNameEnum(name, size, index);
}

OMX_ERRORTYPE OMXMRVLCodecsPlugin::getRolesOfComponent(
        const char *name,
        Vector<String8> *roles) {
    roles->clear();

    OMX_U32 numRoles;
    OMX_ERRORTYPE err =
        _OMX_MasterGetRolesOfComponent(
                const_cast<OMX_STRING>(name),
                &numRoles,
                NULL);

    if (err != OMX_ErrorNone || numRoles == 0) {
        return err;
    }

    if (numRoles > 0) {
        OMX_U8 **array = new OMX_U8 *[numRoles];
        for (OMX_U32 i = 0; i < numRoles; ++i) {
            array[i] = new OMX_U8[OMX_MAX_STRINGNAME_SIZE];
        }

        OMX_U32 numRoles2 = numRoles;
        err = _OMX_MasterGetRolesOfComponent(
                const_cast<OMX_STRING>(name), &numRoles2, array);

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

