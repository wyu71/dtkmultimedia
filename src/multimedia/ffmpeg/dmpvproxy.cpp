// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "config.h"
#include "dmpvproxy.h"
#include "dcompositemanager.h"
#include <mpv/client.h>

#include <random>
#include <QtCore>
#include <QtGlobal>

#include <xcb/xproto.h>
#include <xcb/xcb_aux.h>
#include <QX11Info>
#include <QLibrary>
#include <va/va_x11.h>

DMULTIMEDIA_BEGIN_NAMESPACE

enum AsyncReplyTag {
    SEEK,
    CHANNEL,
    SPEED
};
typedef enum {
  UN_KNOW = 0, //初始值
  MPEG1 , //下面为各种视频格式
  MPEG2,
  MPEG4,
  H264,
  VC1 ,
  DIVX4 ,
  DIVX5,
  HEVC,
  _MAXNULL //超限处理
}decoder_profile; //视频格式解码请求值
typedef  int VdpBool;
typedef enum  {
  decoder_profiles_MPEG1 = 0,   //     {"MPEG1", VDP_DECODER_PROFILE_MPEG1},
  decoder_profiles_MPEG2_SIMPLE,    //     {"MPEG2_SIMPLE", VDP_DECODER_PROFILE_MPEG2_SIMPLE},
  decoder_profiles_MPEG2_MAIN ,  //     {"MPEG2_MAIN", VDP_DECODER_PROFILE_MPEG2_MAIN},
  decoder_profiles_H264_BASELINE,    //     {"H264_BASELINE", VDP_DECODER_PROFILE_H264_BASELINE},
  decoder_profiles_H264_MAIN,    //     {"H264_MAIN", VDP_DECODER_PROFILE_H264_MAIN},
  decoder_profiles_H264_HIGH,    //     {"H264_HIGH", VDP_DECODER_PROFILE_H264_HIGH},
  decoder_profiles_VC1_SIMPLE ,   //     {"VC1_SIMPLE", VDP_DECODER_PROFILE_VC1_SIMPLE},
  decoder_profiles_VC1_MAIN,    //     {"VC1_MAIN", VDP_DECODER_PROFILE_VC1_MAIN},
  decoder_profiles_VC1_ADVANCED ,   //     {"VC1_ADVANCED", VDP_DECODER_PROFILE_VC1_ADVANCED},
  decoder_profiles_MPEG4_PART2_SP,    //     {"MPEG4_PART2_SP", VDP_DECODER_PROFILE_MPEG4_PART2_SP},
  decoder_profiles_MPEG4_PART2_ASP,    //     {"MPEG4_PART2_ASP", VDP_DECODER_PROFILE_MPEG4_PART2_ASP},
  decoder_profiles_DIVX4_QMOBILE,    //     {"DIVX4_QMOBILE", VDP_DECODER_PROFILE_DIVX4_QMOBILE},
  decoder_profiles_DIVX4_MOBILE,    //     {"DIVX4_MOBILE", VDP_DECODER_PROFILE_DIVX4_MOBILE},
  decoder_profiles_DIVX4_HOME_THEATER ,   //     {"DIVX4_HOME_THEATER", VDP_DECODER_PROFILE_DIVX4_HOME_THEATER},
  decoder_profiles_DIVX4_HD_1080P ,   //     {"DIVX4_HD_1080P", VDP_DECODER_PROFILE_DIVX4_HD_1080P},
  decoder_profiles_DIVX5_QMOBILE,    //     {"DIVX5_QMOBILE", VDP_DECODER_PROFILE_DIVX5_QMOBILE},
  decoder_profiles_DIVX5_MOBILE ,   //     {"DIVX5_MOBILE", VDP_DECODER_PROFILE_DIVX5_MOBILE},
  decoder_profiles_DIVX5_HOME_THEATER,    //     {"DIVX5_HOME_THEATER", VDP_DECODER_PROFILE_DIVX5_HOME_THEATER},
  decoder_profiles_DIVX5_HD_1080P,    //     {"DIVX5_HD_1080P", VDP_DECODER_PROFILE_DIVX5_HD_1080P},
  decoder_profiles_H264_CONSTRAINED_BASELINE ,   //     {"H264_CONSTRAINED_BASELINE", VDP_DECODER_PROFILE_H264_CONSTRAINED_BASELINE},
  decoder_profiles_H264_EXTENDED ,   //     {"H264_EXTENDED", VDP_DECODER_PROFILE_H264_EXTENDED},
  decoder_profiles_H264_PROGRESSIVE_HIGH,    //     {"H264_PROGRESSIVE_HIGH", VDP_DECODER_PROFILE_H264_PROGRESSIVE_HIGH},
  decoder_profiles_H264_CONSTRAINED_HIGH,    //     {"H264_CONSTRAINED_HIGH", VDP_DECODER_PROFILE_H264_CONSTRAINED_HIGH},
  decoder_profiles_H264_HIGH_444_PREDICTIVE,    //     {"H264_HIGH_444_PREDICTIVE", VDP_DECODER_PROFILE_H264_HIGH_444_PREDICTIVE},
  decoder_profiles_HEVC_MAIN ,   //     {"HEVC_MAIN", VDP_DECODER_PROFILE_HEVC_MAIN},
  decoder_profiles_HEVC_MAIN_10,    //     {"HEVC_MAIN_10", VDP_DECODER_PROFILE_HEVC_MAIN_10},
  decoder_profiles_HEVC_MAIN_STILL,    //     {"HEVC_MAIN_STILL", VDP_DECODER_PROFILE_HEVC_MAIN_STILL},
  decoder_profiles_HEVC_MAIN_12,    //     {"HEVC_MAIN_12", VDP_DECODER_PROFILE_HEVC_MAIN_12},
  decoder_profiles_HEVC_MAIN_444,    //     {"HEVC_MAIN_444", VDP_DECODER_PROFILE_HEVC_MAIN_444},
  _decoder_maxnull
}VDP_Decoder_e;
#define  RET_INFO_LENTH_MAX  (512)
typedef struct  {
  VDP_Decoder_e  func; //具体值的功能查询
  VdpBool is_supported; //是否支持具体值硬解码
  uint32_t max_width;//最大支持视频宽度
  uint32_t max_height;//最大支持视频高度
  uint32_t max_level; //最大支持等级
  uint32_t max_macroblocks;//最大宏块大小
  char ret_info[RET_INFO_LENTH_MAX];//支持的列表
}VDP_Decoder_t;
struct nodeAutofree {
    mpv_node *pNode;
    explicit nodeAutofree(mpv_node *pValue) : pNode(pValue) {}
    ~nodeAutofree()
    {
        mpv_freeNode_contents(pNode);
    }
};
//返回值大于0表示支持硬解， index 视频格式解码请求值， result 返回解码支持信息
typedef unsigned int (*gpu_decoderInfo)(decoder_profile index, VDP_Decoder_t *result );

static QString libPath(const QString &sLib)
{
    QDir dir;
    QString path  = QLibraryInfo::location(QLibraryInfo::LibrariesPath);
    dir.setPath(path);
    QStringList list = dir.entryList(QStringList() << (sLib + "*"), QDir::NoDotAndDotDot | QDir::Files); //filter name with strlib
    if (list.contains(sLib)) {
        return sLib;
    } else {
        list.sort();
    }

    if(list.size() > 0)
        return list.last();
    else
        return QString();
}


MpvHandle::container::container(mpv_handle *pHandle) : m_pHandle(pHandle) 
{

}

MpvHandle::container::~container()
{
    mpv_terminateDestroy func = (mpv_terminateDestroy)QLibrary::resolve(libPath("libmpv.so.1"), "mpv_terminate_destroy");
    func(m_pHandle);
}

MpvHandle MpvHandle::fromRawHandle(mpv_handle *pHandle)
{
    MpvHandle mpvHandle;
    mpvHandle.sptr = QSharedPointer<container>(new container(pHandle));
    return mpvHandle;
}

static void mpv_callback(void *d)
{
    DMpvProxy *pMpv = static_cast<DMpvProxy *>(d);
    QMetaObject::invokeMethod(pMpv, "has_mpv_events", Qt::QueuedConnection);
}

DMpvProxy::DMpvProxy(QObject *parent)
    : DPlayerBackend(parent)
{
    qRegisterMetaType<MpvHandle>("MpvHandle");
    initMember();

#if defined (__mips__) || defined (__aarch64__)
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
#endif
}

DMpvProxy::~DMpvProxy()
{
    disconnect(this, &DMpvProxy::has_mpv_events, this, &DMpvProxy::handle_mpv_events);
    m_bConnectStateChange = false;
    if (DCompositeManager::get().composited()) {
        disconnect(this, &DMpvProxy::stateChanged, nullptr, nullptr);
    }
}

MpvHandle DMpvProxy::getMpvHandle()
{
    return m_handle;
}

void DMpvProxy::setDecodeModel(const QVariant &value)
{
    m_decodeMode = static_cast<DecodeMode>(value.toInt());
}

void DMpvProxy::initMpvFuns()
{
    QLibrary mpvLibrary(libPath("libmpv.so.1"));

    m_waitEvent = reinterpret_cast<mpv_waitEvent>(mpvLibrary.resolve("mpv_wait_event"));
    m_setOptionString = reinterpret_cast<mpv_set_optionString>(mpvLibrary.resolve("mpv_set_option_string"));
    m_setProperty = reinterpret_cast<mpv_setProperty>(mpvLibrary.resolve("mpv_set_property"));
    m_setPropertyAsync = reinterpret_cast<mpv_setProperty_async>(mpvLibrary.resolve("mpv_set_property_async"));
    m_commandNode = reinterpret_cast<mpv_commandNode>(mpvLibrary.resolve("mpv_command_node"));
    m_commandNodeAsync = reinterpret_cast<mpv_commandNode_async>(mpvLibrary.resolve("mpv_command_node_async"));
    m_getProperty = reinterpret_cast<mpv_getProperty>(mpvLibrary.resolve("mpv_get_property"));
    m_observeProperty = reinterpret_cast<mpv_observeProperty>(mpvLibrary.resolve("mpv_observe_property"));
    m_eventName = reinterpret_cast<mpv_eventName>(mpvLibrary.resolve("mpv_event_name"));
    m_creat = reinterpret_cast<mpvCreate>(mpvLibrary.resolve("mpv_create"));
    m_requestLogMessage = reinterpret_cast<mpv_requestLog_messages>(mpvLibrary.resolve("mpv_request_log_messages"));
    m_setWakeupCallback = reinterpret_cast<mpv_setWakeup_callback>(mpvLibrary.resolve("mpv_set_wakeup_callback"));
    m_initialize = reinterpret_cast<mpvinitialize>(mpvLibrary.resolve("mpv_initialize"));
    m_freeNodecontents = reinterpret_cast<mpv_freeNode_contents>(mpvLibrary.resolve("mpv_free_node_contents"));
}

void DMpvProxy::initGpuInfoFuns()
{
    QString path = QLibraryInfo::location(QLibraryInfo::LibrariesPath)+ QDir::separator() + "libgpuinfo.so";
    if(!QFileInfo(path).exists()) {
        m_gpuInfo = NULL;
        return;
    }
    QLibrary mpvLibrary(libPath("libgpuinfo.so"));
    m_gpuInfo = reinterpret_cast<void *>(mpvLibrary.resolve("vdp_Iter_decoderInfo"));
}

void DMpvProxy::firstInit()
{
    initMpvFuns();
    initGpuInfoFuns();
    if (m_creat) {
        m_handle = MpvHandle::fromRawHandle(mpv_init());
        if (DCompositeManager::get().composited()) {
            emit notifyCreateOpenGL(m_handle);
        }
    }

    m_bInited = true;
    initSetting();
}

void DMpvProxy::initSetting()
{
    QMapIterator<QString, QVariant> mapItor(m_mapWaitSet);
    while (mapItor.hasNext()) {
        mapItor.next();
        my_set_property(m_handle, mapItor.key(), mapItor.value());
    }

    QVectorIterator<QVariant> vecItor(m_vecWaitCommand);
    while (vecItor.hasNext()) {
        my_command(m_handle, vecItor.peekNext());
        vecItor.next();
    }
}

mpv_handle *DMpvProxy::mpv_init()
{
    mpv_handle *pHandle =  static_cast<mpv_handle *>(m_creat());
    bool composited = DCompositeManager::get().composited();
    switch (m_debugLevel) {
    case DebugLevel::Info:
        m_requestLogMessage(pHandle, "info");
        break;

    case DebugLevel::Debug:
    case DebugLevel::Verbose:
        my_set_property(pHandle, "terminal", "yes");
        if (m_debugLevel == DebugLevel::Verbose) {
            my_set_property(pHandle, "msg-level", "all=status");
            m_requestLogMessage(pHandle, "info");

        } else {
            my_set_property(pHandle, "msg-level", "all=v");
            m_requestLogMessage(pHandle, "v");
        }
        break;
    }
    if (composited) {
        auto interop = QString::fromUtf8("vaapi-glx");
        if (!qEnvironmentVariableIsEmpty("QT_XCB_GL_INTERGRATION")) {
            auto gl_int = qgetenv("QT_XCB_GL_INTERGRATION");
            if (gl_int == "xcb_egl") {
                interop = "vaapi-egl";
            } else if (gl_int == "xcb_glx") {
                interop = "vaapi-glx";
            } else {
                interop = "auto";
            }
        }
        my_set_property(pHandle, "gpu-hwdec-interop", interop.toUtf8().constData());
        qInfo() << "set gpu-hwdec-interop = " << interop;
    }
    my_set_property(pHandle, "hwdec", "auto");

#ifdef __aarch64__
    if (DCompositeManager::get().isOnlySoftDecode()) {
        my_set_property(pHandle, "hwdec", "no");
    } else {
        my_set_property(pHandle, "hwdec", "auto");
    }
    qInfo() << "modify HWDEC auto";
#endif

    my_set_property(pHandle, "panscan", 1.0);
    if (DecodeMode::SOFTWARE == m_decodeMode) { //1.设置软解
        my_set_property(m_handle, "hwdec", "no");
    } else if (DecodeMode::AUTO == m_decodeMode) { //2.设置自动
        //2.1特殊硬件
        //景嘉微显卡目前只支持vo=xv，等日后升级代码需要酌情修改。
        QFileInfo fi("/dev/mwv206_0");
        QFileInfo jmfi("/dev/jmgpu"); //jmgpu
        if (fi.exists() || jmfi.exists()) { //2.1.1景嘉微
            QDir sdir(QLibraryInfo::location(QLibraryInfo::LibrariesPath) +QDir::separator() +"mwv206"); //判断是否安装核外驱动
            QDir jmdir(QLibraryInfo::location(QLibraryInfo::LibrariesPath) +QDir::separator() +"mwv207");
            if(sdir.exists()) {
                my_set_property(m_handle, "hwdec", "vdpau");
            } else {
                my_set_property(m_handle, "hwdec", "auto");
            }

            if (!sdir.exists() && jmdir.exists()) {
                my_set_property(m_handle, "hwdec", "vaapi");
                my_set_property(m_handle, "vo", "vaapi");
                m_sInitVo = "vaapi";
            } else {
                my_set_property(m_handle, "vo", "vdpau,xv,x11");
                m_sInitVo = "vdpau,xv,x11";
            }
        } else if (QFile::exists("/dev/csmcore")) { //2.1.2中船重工
            my_set_property(m_handle, "vo", "xv,x11");
            my_set_property(m_handle, "hwdec", "auto");
            if (DCompositeManager::get().check_wayland_env()) {
                my_set_property(pHandle, "wid", m_winId);
            }
            m_sInitVo = "xv,x11";
        } else if (DCompositeManager::get().isOnlySoftDecode()) {//2.1.3 鲲鹏920 || 曙光+英伟达 || 浪潮
            my_set_property(m_handle, "hwdec", "no");
        } else { //2.2非特殊硬件
            my_set_property(m_handle, "hwdec", "auto");
        }

#if defined (__mips__)
        if (!DCompositeManager::get().hascard()) {
            qInfo() << "修改音视频同步模式";
            my_set_property(m_handle, "video-sync", "desync");
        }
        my_set_property(m_handle, "vo", "vdpau,gpu,x11");
        my_set_property(m_handle, "ao", "alsa");
        m_sInitVo = "vdpau,gpu,x11";
#elif defined(_loongarch) || defined(__loongarch__) || defined(__loongarch64)
        if (!DCompositeManager::get().hascard()) {
            qInfo() << "修改音视频同步模式";
            my_set_property(m_handle, "video-sync", "desync");
        }
        if (!fi.exists() && !jmfi.exists()) {
            my_set_property(m_handle, "vo", "gpu,x11");
            m_sInitVo = "gpu,x11";
        }
#elif defined (__sw_64__)
        //Synchronously modify the video output of the SW platform vdpau(powered by zhangfl)
        my_set_property(m_handle, "vo", "vdpau,gpu,x11");
        m_sInitVo = "vdpau,gpu,x11";
#elif defined (__aarch64__)
        if (!fi.exists() && !jmfi.exists()) { //2.1.1景嘉微
            my_set_property(m_handle, "vo", "gpu,xv,x11");
            m_sInitVo = "gpu,xv,x11";
        }
#else
        //去除9200显卡适配
        QFileInfo sjmfi("/dev/jmgpu");
        bool jmflag = false;
        if (sjmfi.exists()) {
            QDir jmdir(QLibraryInfo::location(QLibraryInfo::LibrariesPath) +QDir::separator() +"mwv207");
            if(jmdir.exists())
            {
                jmflag=true;
            }
        }
        //TODO(xxxxpengfei)：暂未处理intel集显情况
        if (DCompositeManager::get().isZXIntgraphics() && !jmflag) {
            my_set_property(m_handle, "vo", "gpu");
        }
#endif
    } else { //3.设置硬解
        QFileInfo fi("/dev/mwv206_0");
        QFileInfo jmfi("/dev/jmgpu");
        if (fi.exists() || jmfi.exists()) { //2.1.1景嘉微
            QDir sdir(QLibraryInfo::location(QLibraryInfo::LibrariesPath) +QDir::separator() +"mwv206"); //判断是否安装核外驱动
            QDir jmdir(QLibraryInfo::location(QLibraryInfo::LibrariesPath) +QDir::separator() +"mwv207");
            if(sdir.exists()) {
                 my_set_property(m_handle, "hwdec", "vdpau");
            } else {
                 my_set_property(m_handle, "hwdec", "auto");
            }
            if (!sdir.exists() && jmdir.exists()) {
                my_set_property(m_handle, "hwdec", "vaapi");
                my_set_property(m_handle, "vo", "vaapi");
                m_sInitVo = "vaapi";
            } else {
                my_set_property(m_handle, "vo", "vdpau,xv,x11");
                m_sInitVo = "vdpau,xv,x11";
            }
        } else {
            my_set_property(m_handle, "hwdec", "auto");
        }
    }

    if (composited) {
#ifdef __mips__
        m_setOptionString(pHandle, "vo", "opengl-cb");
        m_setOptionString(pHandle, "hwdec-preload", "auto");
        m_setOptionString(pHandle, "opengl-hwdec-interop", "auto");
        m_setOptionString(pHandle, "hwdec", "auto");
        qInfo() << "-------- __mips__hwdec____________";
        m_sInitVo = "opengl-cb";
#else
        my_set_property(pHandle, "vo", "libmpv,opengl-cb");
        my_set_property(pHandle, "vd-lavc-dr", "no");
        my_set_property(pHandle, "gpu-sw", "on");
        m_sInitVo = "libmpv,opengl-cb";
#endif
    } else {
        my_set_property(m_handle, "wid", m_winId);
    }

    qInfo() << __func__ << "vo:" << my_get_property(pHandle, "vo").toString();
    qInfo() << __func__  << "hwdec:" << my_get_property(pHandle, "hwdec").toString();

    //my_set_property(pHandle, "keepaspect-window", "no");
    //设置视频固定帧率，暂时无效
    //my_set_property(pHandle, "correct-pts", false);
    //my_set_property(pHandle, "fps", 30);
    my_set_property(pHandle, "panscan", 0);
    my_set_property(pHandle, "volume-max", 200.0);
    my_set_property(pHandle, "input-cursor", "no");
    my_set_property(pHandle, "cursor-autohide", "no");
    my_set_property(pHandle, "sub-auto", "fuzzy");
    my_set_property(pHandle, "sub-visibility", "true");
    my_set_property(pHandle, "sub-pos", 100);
    my_set_property(pHandle, "sub-margin-y", 36);
    my_set_property(pHandle, "sub-border-size", 0);
    my_set_property(pHandle, "screenshot-template", "deepin-movie-shot%n");
    my_set_property(pHandle, "screenshot-directory", "/tmp");

    //only to get notification without data
    m_observeProperty(pHandle, 0, "time-pos", MPV_FORMAT_NONE); //playback-time ?
    m_observeProperty(pHandle, 0, "pause", MPV_FORMAT_NONE);
    m_observeProperty(pHandle, 0, "mute", MPV_FORMAT_NONE);
    m_observeProperty(pHandle, 0, "volume", MPV_FORMAT_NONE); //ao-volume ?
    m_observeProperty(pHandle, 0, "sid", MPV_FORMAT_NONE);
    m_observeProperty(pHandle, 0, "aid", MPV_FORMAT_NODE);
    m_observeProperty(pHandle, 0, "dwidth", MPV_FORMAT_NODE);
    m_observeProperty(pHandle, 0, "dheight", MPV_FORMAT_NODE);

    // because of vpu, we need to implement playlist w/o mpv
    //m_observeProperty(pHandle, 0, "playlist-pos", MPV_FORMAT_NONE);
    //m_observeProperty(pHandle, 0, "playlist-count", MPV_FORMAT_NONE);
    m_observeProperty(pHandle, 0, "core-idle", MPV_FORMAT_NODE);
    m_observeProperty(pHandle, 0, "paused-for-cache", MPV_FORMAT_NODE);

    m_setWakeupCallback(pHandle, mpv_callback, this);
    connect(this, &DMpvProxy::has_mpv_events, this, &DMpvProxy::handle_mpv_events,
            Qt::DirectConnection);
    if (m_initialize(pHandle) < 0) {
        std::runtime_error("mpv init failed");
    }

    //load profile
    auto ol = DCompositeManager::get().getBestProfile();
    auto p = ol.begin();
    while (p != ol.end()) {
        if (!p->first.startsWith("#")) {
#if !defined (__mips__ ) && !defined(__aarch64__) && !defined(__sw_64__)
#ifdef MWV206_0
            QFileInfo fi("/dev/mwv206_0");              //景嘉微显卡目前只支持vo=xv，等日后升级代码需要酌情修改。
            QFileInfo jmfi("/dev/jmgpu"); //jmgpu
            if (!fi.exists() && !jmfi.exists()) {
                my_set_property(pHandle, p->first.toUtf8().constData(), p->second.toUtf8().constData());
                qInfo() << "apply" << p->first << "=" << p->second;
            }
#else
            my_set_property(pHandle, p->first.toUtf8().constData(), p->second.toUtf8().constData());
            qInfo() << "apply" << p->first << "=" << p->second;
#endif
#endif
        } else {
            qInfo() << "ignore(commented out)" << p->first << "=" << p->second;
        }
        ++p;
    }

    return pHandle;
}

void DMpvProxy::setState(PlayState state)
{
    bool bRawFormat = false;

//    if (0 < dynamic_cast<PlayerEngine *>(m_pParentWidget)->getplaylist()->size()) {
//        PlayItemInfo currentInfo = dynamic_cast<PlayerEngine *>(m_pParentWidget)->getplaylist()->currentInfo();
//        bRawFormat = currentInfo.mi.isRawFormat();
//    }

    if (m_state != state) {
        m_state = state;
        emit stateChanged();
    }
}

void DMpvProxy::pollingEndOfPlayback()
{
    if (m_state != DPlayerBackend::Stopped) {
        m_bPolling = true;
        blockSignals(true);
        stop();
        bool bIdel = my_get_property(m_handle, "idle-active").toBool();
        if (bIdel) {
            blockSignals(false);
            setState(DPlayerBackend::Stopped);
            m_bPolling = false;
            return;
        }

        while (m_state != DPlayerBackend::Stopped) {
            mpv_event *pEvent = m_waitEvent(m_handle, 0.005);
            if (pEvent->event_id == MPV_EVENT_NONE)
                continue;

            if (pEvent->event_id == MPV_EVENT_END_FILE) {
                blockSignals(false);
                setState(DPlayerBackend::Stopped);
                break;
            }
        }
        m_bPolling = false;
    }
}

const PlayingMovieInfo &DMpvProxy::playingMovieInfo()
{
    return m_movieInfo;
}

bool DMpvProxy::isPlayable() const
{
    return true;
}

bool DMpvProxy::isSurportHardWareDecode(const QString sDecodeName, const int &nVideoWidth, const int &nVideoHeight)
{
    bool isHardWare = true;//未安装探测工具默认支持硬解
    decoder_profile decoderValue = decoder_profile::UN_KNOW; //初始化支持解码值
    decoderValue = (decoder_profile)getDecodeProbeValue(sDecodeName); //根据视频格式获取解码值
    if(decoderValue != decoder_profile::UN_KNOW ) {//开始探测是否支持硬解码
        VDP_Decoder_t *probeDecode = new VDP_Decoder_t;
        if(m_gpuInfo) {
            int nSurport =  ((gpu_decoderInfo)m_gpuInfo)(decoderValue, probeDecode);
            isHardWare = (nSurport > 0 && probeDecode->max_width >= nVideoWidth
                    &&  probeDecode->max_height >= nVideoHeight);//nSurport大于0表示支持，硬解码支持的最大宽高必须大于或等于视频的宽高
        }
        delete probeDecode;
    }
    return isHardWare;
}

int DMpvProxy::getDecodeProbeValue(const QString sDecodeName)
{
    QStringList sNameList;
    sNameList << "MPEG1" << "MPEG2" << "MPEG4" << "H264" << "VC1" << "DIVX4" << "DIVX5" << "HEVC";
    int nCount = sNameList.count();
    for(int i = 0; i < nCount; i++ ){//匹配硬解支持的视频格式
        QString sValue = sNameList.at(i);
        if(sDecodeName.toUpper().contains(sValue)) {
            return (int)decoder_profile(decoder_profile::UN_KNOW + 1 + i);
        }
    }
    return (int)decoder_profile::UN_KNOW;
}

void DMpvProxy::setWinID(const qint64 &winID)
{
    m_winId = winID;
}

void DMpvProxy::handle_mpv_events()
{
    if (DCompositeManager::get().check_wayland_env() && DCompositeManager::get().isTestFlag()) {
        qInfo() << "not handle mpv events!";
        return;
    }
    while (1) {
        mpv_event *pEvent = m_waitEvent(m_handle, 0.0005);
        if (pEvent->event_id == MPV_EVENT_NONE)
            break;

        switch (pEvent->event_id) {
        case MPV_EVENT_LOG_MESSAGE:
            processLogMessage(reinterpret_cast<mpv_event_log_message *>(pEvent->data));
            break;

        case MPV_EVENT_PROPERTY_CHANGE:
            processPropertyChange(reinterpret_cast<mpv_event_property *>(pEvent->data));
            break;

        case MPV_EVENT_COMMAND_REPLY:
            if (pEvent->error < 0) {
                qInfo() << "command error";
            }

            if (pEvent->reply_userdata == AsyncReplyTag::SEEK) {
                m_bPendingSeek = false;
            }
            break;

        case MPV_EVENT_PLAYBACK_RESTART:
            // caused by seek or just playing
            break;

#if MPV_CLIENT_API_VERSION < MPV_MAKE_VERSION(2,0)
        case MPV_EVENT_TRACKS_CHANGED:
            qInfo() << m_eventName(pEvent->event_id);
            updatePlayingMovieInfo();
            emit tracksChanged();
            break;
#endif

        case MPV_EVENT_FILE_LOADED: {
            qInfo() << m_eventName(pEvent->event_id);

            if (m_winId != -1) {
                qInfo() << "hwdec-interop" << my_get_property(m_handle, "gpu-hwdec-interop")
                        << "codec: " << my_get_property(m_handle, "video-codec")
                        << "format: " << my_get_property(m_handle, "video-format");
            }

            setState(PlayState::Playing); //might paused immediately
            emit fileLoaded();
            qInfo() << QString("rotate metadata: dec %1, out %2")
                    .arg(my_get_property(m_handle, "video-dec-params/rotate").toInt())
                    .arg(my_get_property(m_handle, "video-params/rotate").toInt());
            break;
        }
        case MPV_EVENT_VIDEO_RECONFIG: {
            QSize size = videoSize();
            if (!size.isEmpty())
                emit videoSizeChanged();
            break;
        }

        case MPV_EVENT_END_FILE: {
            mpv_event_end_file *ev_ef = reinterpret_cast<mpv_event_end_file *>(pEvent->data);
            qInfo() << m_eventName(pEvent->event_id) <<
                    "reason " << ev_ef->reason;

            setState(PlayState::Stopped);
            break;
        }

        case MPV_EVENT_IDLE:
            qInfo() << m_eventName(pEvent->event_id);
            setState(PlayState::Stopped);
            emit elapsedChanged();
            break;

        default:
            qInfo() << m_eventName(pEvent->event_id);
            break;
        }
    }
}

void DMpvProxy::processLogMessage(mpv_event_log_message *pEvent)
{
    switch (pEvent->log_level) {
    case MPV_LOG_LEVEL_WARN:
        qWarning() << QString("%1: %2").arg(pEvent->prefix).arg(pEvent->text);
        emit mpvWarningLogsChanged(QString(pEvent->prefix), QString(pEvent->text));
        break;

    case MPV_LOG_LEVEL_ERROR:
    case MPV_LOG_LEVEL_FATAL: {
        QString strError = pEvent->text;
        if (strError.contains("Failed setup for format vdpau")) {
            m_bLastIsSpecficFormat = true;
        }
        qCritical() << QString("%1: %2").arg(pEvent->prefix).arg(strError);
        emit mpvErrorLogsChanged(QString(pEvent->prefix), strError);
    }
    break;

    case MPV_LOG_LEVEL_INFO:
        qInfo() << QString("%1: %2").arg(pEvent->prefix).arg(pEvent->text);
        break;

    default:
        qInfo() << QString("%1: %2").arg(pEvent->prefix).arg(pEvent->text);
        break;
    }
}

void DMpvProxy::processPropertyChange(mpv_event_property *pEvent)
{
    QString sName = QString::fromUtf8(pEvent->name);
    if (sName != "time-pos") qInfo() << sName;

    if (sName == "time-pos") {
        emit elapsedChanged();
    } else if (sName == "volume") {
        emit volumeChanged();
    } else if (sName == "dwidth" || sName == "dheight") {
        auto sz = videoSize();
        if (!sz.isEmpty())
            emit videoSizeChanged();
        qInfo() << "update videoSize " << sz;
    } else if (sName == "aid") {
        emit aidChanged();
    } else if (sName == "sid") {
        emit sidChanged();
    } else if (sName == "mute") {
        emit muteChanged();
    } else if (sName == "sub-visibility") {
        //_hideSub = my_get_property(m_handle, "sub-visibility")
    } else if (sName == "pause") {
        auto idle = my_get_property(m_handle, "idle-active").toBool();
        if (my_get_property(m_handle, "pause").toBool()) {
            if (!idle)
                setState(PlayState::Paused);
            else
                my_set_property(m_handle, "pause", false);
        } else {
            if (state() != PlayState::Stopped) {
                setState(PlayState::Playing);
            }
        }
    } else if (sName == "core-idle") {
    } else if (sName == "paused-for-cache") {
        qInfo() << "paused-for-cache" << my_get_property_variant(m_handle, "paused-for-cache");
        emit urlpause(my_get_property_variant(m_handle, "paused-for-cache").toBool());
    }
}

bool DMpvProxy::loadSubtitle(const QFileInfo &fileInfo)
{
    //movie could be in an inner state that marked as Stopped when loadfile executes
    //if (state() == PlayState::Stopped) { return true; }
    if (!fileInfo.exists())
        return false;

    QList<QVariant> args = { "sub-add", fileInfo.absoluteFilePath(), "select" };
    qInfo() << args;
    QVariant id = my_command(m_handle, args);
    if (id.canConvert<ErrorReturn>()) {
        return false;
    }

    updatePlayingMovieInfo();

    // by settings this flag, we can match the corresponding sid change and save it
    // in the movie database
    return true;
}

bool DMpvProxy::isSubVisible()
{
    return my_get_property(m_handle, "sub-visibility").toBool();
}

void DMpvProxy::setSubDelay(double dSecs)
{
    my_set_property(m_handle, "sub-delay", dSecs);
}

double DMpvProxy::subDelay() const
{
    return my_get_property(m_handle, "sub-delay").toDouble();
}

QString DMpvProxy::subCodepage()
{
    auto cp = my_get_property(m_handle, "sub-codepage").toString();
    if (cp.startsWith("+")) {
        cp.remove(0, 1);
    }

    return cp;
}

void DMpvProxy::addSubSearchPath(const QString &sPath)
{
    my_set_property(m_handle, "sub-paths", sPath);
    my_set_property(m_handle, "sub-file-paths", sPath);
}

void DMpvProxy::setSubCodepage(const QString &sCodePage)
{
    QString strTmp = sCodePage;
    if (!sCodePage.startsWith("+") && sCodePage != "auto")
        strTmp.prepend('+');

    my_set_property(m_handle, "sub-codepage", strTmp);
    my_command(m_handle, {"sub-reload"});
}

void DMpvProxy::updateSubStyle(const QString &sFont, int nSize)
{
    my_set_property(m_handle, "sub-font", sFont);
    my_set_property(m_handle, "sub-font-size", nSize);
    my_set_property(m_handle, "sub-color", "#FFFFFF");
    my_set_property(m_handle, "sub-border-size", 1);
    my_set_property(m_handle, "sub-border-color", "0.0/0.0/0.0/0.50");
    my_set_property(m_handle, "sub-shadow-offset", 1);
    my_set_property(m_handle, "sub-shadow-color", "0.0/0.0/0.0/0.50");
}

void DMpvProxy::savePlaybackPosition()
{
    if (state() == PlayState::Stopped) {
        return;
    }
}

void DMpvProxy::setPlaySpeed(double dTimes)
{
    if(m_handle)
        my_set_property_async(m_handle, "speed", dTimes, AsyncReplyTag::SPEED);
}

void DMpvProxy::selectSubtitle(int nId)
{
    if (nId > m_movieInfo.subs.size()) {
        nId = m_movieInfo.subs.size() == 0 ? -1 : m_movieInfo.subs[0]["id"].toInt();
    }

    my_set_property(m_handle, "sid", nId);
}

void DMpvProxy::toggleSubtitle()
{
    if (state() == PlayState::Stopped) {
        return;
    }

    my_set_property(m_handle, "sub-visibility", !isSubVisible());
}

int DMpvProxy::aid() const
{
    return my_get_property(m_handle, "aid").toInt();
}

int DMpvProxy::sid() const
{
    return my_get_property(m_handle, "sid").toInt();
}

void DMpvProxy::selectTrack(int nId)
{
    if (nId >= m_movieInfo.audios.size()) return;
    QVariant aid  = m_movieInfo.audios[nId]["id"];
    my_set_property(m_handle, "aid", aid);
}

void DMpvProxy::changeSoundMode(SoundMode soundMode)
{
    QList<QVariant> listArgs;

    switch (soundMode) {
    case SoundMode::Stereo:
        listArgs << "af" << "set" << "stereotools=muter=false";
        break;
    case SoundMode::Left:
        listArgs << "af" << "set" << "stereotools=muter=true";
        break;
    case SoundMode::Right:
        listArgs << "af" << "set" << "stereotools=mutel=true";
        break;
    }

    my_command(m_handle, listArgs);
}

void DMpvProxy::volumeUp()
{
    if (volume() >= 200)
        return;

    changeVolume(volume() + 10);
}

void DMpvProxy::changeVolume(int nVol)
{
    my_set_property(m_handle, "volume", volumeCorrection(nVol));
}

void DMpvProxy::volumeDown()
{
    if (volume() <= 0)
        return;

    changeVolume(volume() - 10);
}

int DMpvProxy::volume() const
{
    int nActualVol = my_get_property(m_handle, "volume").toInt();
    int nDispalyVol = static_cast<int>((nActualVol - 40) / 60.0 * 100.0);
    return nDispalyVol > 100 ? nActualVol : nDispalyVol;
}

int DMpvProxy::videoRotation() const
{
    int nRotate = my_get_property(m_handle, "video-rotate").toInt();
    return (nRotate + 360) % 360;
}

void DMpvProxy::setVideoRotation(int nDegree)
{
    my_set_property(m_handle, "video-rotate", nDegree);
}

void DMpvProxy::setVideoAspect(double dValue)
{
    my_set_property(m_handle, "video-aspect", dValue);
}

double DMpvProxy::videoAspect() const
{
    return my_get_property(m_handle, "video-aspect").toDouble();
}

bool DMpvProxy::muted() const
{
    return my_get_property(m_handle, "mute").toBool();
}

void DMpvProxy::toggleMute()
{
    QList<QVariant> listArgs = { "cycle", "mute" };
    qInfo() << listArgs;
    my_command(m_handle, listArgs);
}

void DMpvProxy::setMute(bool bMute)
{
    my_set_property(m_handle, "mute", bMute);
}

void DMpvProxy::refreshDecode()
{
    QList<QString> canHwTypes;
    //bool bIsCanHwDec = HwdecProbe::get().isFileCanHwdec(_file.url(), canHwTypes);

    if (DecodeMode::SOFTWARE == m_decodeMode) { //1.设置软解
        my_set_property(m_handle, "hwdec", "no");
    } else if (DecodeMode::AUTO == m_decodeMode) {//2.设置自动
        //2.1 特殊格式
        bool isSoftCodec = false;
        if (isSoftCodec) {
            qInfo() << "my_set_property hwdec no";
            my_set_property(m_handle, "hwdec", "no");
        } else { //2.2 非特殊格式
            //2.2.1 特殊硬件
            QFileInfo fi("/dev/mwv206_0"); //2.2.1.1 景嘉微
            QFileInfo jmfi("/dev/jmgpu");
            if (fi.exists() || jmfi.exists()) {
                QDir sdir(QLibraryInfo::location(QLibraryInfo::LibrariesPath) +QDir::separator() +"mwv206"); //判断是否安装核外驱动
                QDir jmdir(QLibraryInfo::location(QLibraryInfo::LibrariesPath) +QDir::separator() +"mwv207");
                if(sdir.exists())
                {
                     my_set_property(m_handle, "hwdec", "vdpau");
                }else {
                     my_set_property(m_handle, "hwdec", "auto");
                }
                if (!sdir.exists() && jmdir.exists()) {
                    my_set_property(m_handle, "hwdec", "vaapi");
                }
            } else if (DCompositeManager::get().isOnlySoftDecode()) { //2.2.1.2 鲲鹏920 || 曙光+英伟达 || 浪潮
                my_set_property(m_handle, "hwdec", "no");
            } else { //2.2.2 非特殊硬件 + 非特殊格式
                 my_set_property(m_handle, "hwdec","auto");
            }
        }
    } else { //3.设置硬解
        if (DCompositeManager::get().isOnlySoftDecode()) { // 鲲鹏920 || 曙光+英伟达 || 浪潮
            my_set_property(m_handle, "hwdec", "no");
        } else {
             my_set_property(m_handle, "hwdec","auto");
            //bIsCanHwDec ? my_set_property(m_handle, "hwdec", canHwTypes.join(',')) : my_set_property(m_handle, "hwdec", "no");
        }
        QFileInfo fi("/dev/mwv206_0"); //2.2.1.1 景嘉微
        QFileInfo jmfi("/dev/jmgpu");
        if (fi.exists() || jmfi.exists()) {
            QDir sdir(QLibraryInfo::location(QLibraryInfo::LibrariesPath) +QDir::separator() +"mwv206"); //判断是否安装核外驱动
            QDir jmdir(QLibraryInfo::location(QLibraryInfo::LibrariesPath) +QDir::separator() +"mwv207");
            if(sdir.exists())
            {
                 my_set_property(m_handle, "hwdec", "vdpau");
            } else {
                 my_set_property(m_handle, "hwdec", "auto");
            }
            if (!sdir.exists() && jmdir.exists()) {
                my_set_property(m_handle, "hwdec", "vaapi");
            }
        }
    }
}

void DMpvProxy::initMember()
{
    m_nBurstStart = 0;
    m_bInBurstShotting = false;
    m_posBeforeBurst = false;
    m_bPendingSeek = false;
    m_bPolling = false;
    m_bConnectStateChange = false;
    m_bPauseOnStart = false;
    m_bIsJingJia = false;
    m_bInited = false;
    m_bHwaccelAuto = false;
    m_bLastIsSpecficFormat = false;

    m_sInitVo = "gpu,xv,x11";//初始化vo数据
    m_listBurstPoints.clear();
    m_mapWaitSet.clear();
    m_vecWaitCommand.clear();

    m_waitEvent = nullptr;
    m_setOptionString = nullptr;
    m_setProperty = nullptr;
    m_setPropertyAsync = nullptr;
    m_commandNode = nullptr;
    m_commandNodeAsync = nullptr;
    m_getProperty = nullptr;
    m_observeProperty = nullptr;
    m_eventName = nullptr;
    m_creat = nullptr;
    m_requestLogMessage = nullptr;
    m_setWakeupCallback = nullptr;
    m_initialize = nullptr;
    m_freeNodecontents = nullptr;
    m_pConfig = nullptr;
    m_gpuInfo = nullptr;
}

void DMpvProxy::play()
{
    bool bRawFormat = false;
    QList<QVariant> listArgs = { "loadfile" };
    QStringList listOpts = { };
//    PlayerEngine* pEngine = nullptr;
    bool bAudio = false;

    if (!m_bInited) {
        firstInit();
    }

//    pEngine = dynamic_cast<PlayerEngine *>(m_pParentWidget);
//    if (pEngine && pEngine->getplaylist()->size() > 0) {
//        bRawFormat = pEngine->getplaylist()->currentInfo().mi.isRawFormat();
//        bAudio =  pEngine->currFileIsAudio();
//    }

    if (bAudio) {
        my_set_property(m_handle, "vo", "null");
    } else {
        my_set_property(m_handle, "vo", m_sInitVo);
    }

    if (m_file.isLocalFile()) {
        listArgs << QFileInfo(m_file.toLocalFile()).absoluteFilePath();
    } else {
        listArgs << m_file.url();
    }

    //刷新解码模式
    refreshDecode();

    QFileInfo fi("/dev/mwv206_0");  // 景美驱动硬解avs2有崩溃问题
    if (fi.exists()) {
        QDir sdir(QLibraryInfo::location(QLibraryInfo::LibrariesPath) +QDir::separator() +"mwv206");
    }

    if (listOpts.size()) {
        listArgs << "replace" << listOpts.join(',');
    }

    qInfo() << listArgs;

    my_command(m_handle, listArgs);
    my_set_property(m_handle, "pause", m_bPauseOnStart);
}


void DMpvProxy::pauseResume()
{
    if (m_state == PlayState::Stopped)
        return;

    my_set_property(m_handle, "pause", !paused());
}

void DMpvProxy::stop()
{
    QList<QVariant> args = { "stop" };
    qInfo() << args;
    my_command(m_handle, args);
}

QImage DMpvProxy::takeScreenshot()
{
    return takeOneScreenshot();
}

void DMpvProxy::burstScreenshot()
{
    if (m_bInBurstShotting) {
        qWarning() << "already in burst screenshotting mode";
        return;
    }

    if (state() == PlayState::Stopped)
        return;

    //my_command(m_handle, QList<QVariant> {"revert-seek", "mark"});
    m_posBeforeBurst = my_get_property(m_handle, "time-pos");

    int nDuration = static_cast<int>(duration() / 15);

    std::random_device rd;
    std::mt19937 g(rd());
    std::uniform_int_distribution<int> uniform_dist(0, nDuration);
    m_listBurstPoints.clear();
    for (int i = 0; i < 15; i++) {
        m_listBurstPoints.append(nDuration * i + uniform_dist(g));
    }
    m_nBurstStart = 0;

    if (duration() < 35) {
        emit notifyScreenshot(QImage(), 0);
        stopBurstScreenshot();
        return;
    }
    qInfo() << "burst span " << m_nBurstStart;

    if (!paused()) pauseResume();
    m_bInBurstShotting = true;
    QTimer::singleShot(0, this, &DMpvProxy::stepBurstScreenshot);
}

qint64 DMpvProxy::nextBurstShootPoint()
{
    auto next = m_listBurstPoints[static_cast<int>(m_nBurstStart++)];
    if (next >= duration()) {
        next = duration() - 5;
    }

    return next;
}

int DMpvProxy::volumeCorrection(int displayVol)
{
    int realVol = 0;
    if (displayVol > 100)
        return displayVol;
    realVol = static_cast<int>((displayVol / 100.0) * 60.0 + 40);
    return (realVol == 40 ? 0 : realVol);
}

QVariant DMpvProxy::my_get_property(mpv_handle *pHandle, const QString &sName) const
{
    mpv_node node;
    if (!m_getProperty) return QVariant();
    int err = m_getProperty(pHandle, sName.toUtf8().data(), MPV_FORMAT_NODE, &node);
    if (err < 0)
        return QVariant::fromValue(ErrorReturn(err));
    auto variant = node_to_variant(&node);
    m_freeNodecontents(&node);
    return variant;
}

int DMpvProxy::my_set_property(mpv_handle *pHandle, const QString &sName, const QVariant &v)
{
    QVariant sValue = v;
//   if(sName.compare("hwdec") == 0) {
//       sValue = "no";
//   }

    node_builder node(sValue);

    if (!m_bInited) {
        m_mapWaitSet.insert(sName, sValue);
        return 0;
    }

    if (!m_setProperty) return 0;
    int res = m_setProperty(pHandle, sName.toUtf8().data(), MPV_FORMAT_NODE, node.node());
    return res;
}

bool DMpvProxy::my_command_async(mpv_handle *pHandle, const QVariant &args, uint64_t tag)
{
    node_builder node(args);
    int nErr = m_commandNodeAsync(pHandle, tag, node.node());
    return nErr == 0;
}

int DMpvProxy::my_set_property_async(mpv_handle *pHandle, const QString &sName, const QVariant &value, uint64_t tag)
{
    node_builder node(value);
    return m_setPropertyAsync(pHandle, tag, sName.toUtf8().data(), MPV_FORMAT_NODE, node.node());
}

QVariant DMpvProxy::my_get_property_variant(mpv_handle *pHandle, const QString &sName)
{
    mpv_node node;
    if (m_getProperty(pHandle, sName.toUtf8().data(), MPV_FORMAT_NODE, &node) < 0)
        return QVariant();
    nodeAutofree f(&node);
    return node_to_variant(&node);
}

QVariant DMpvProxy::my_command(mpv_handle *pHandle, const QVariant &args)
{
    if (!m_bInited) {
        m_vecWaitCommand.append(args);
        return QVariant();
    }

    node_builder node(args);
    mpv_node res;
    int nErr = m_commandNode(pHandle, node.node(), &res);
    if (nErr < 0)
        return QVariant::fromValue(ErrorReturn(nErr));
    auto variant = node_to_variant(&res);
    m_freeNodecontents(&res);
    return variant;
}

QImage DMpvProxy::takeOneScreenshot()
{
    bool bNeedRotate = false;
    QString strVO = getProperty("current-vo").toString();  // the image by screenshot wont rotate when vo=vdpau

    if(strVO.compare("vdpau", Qt::CaseInsensitive) == 0) {
        bNeedRotate = true;
    }

    if (state() == PlayState::Stopped) return QImage();

    QList<QVariant> args = {"screenshot-raw"};
    node_builder node(args);
    mpv_node res;
    int nErr = m_commandNode(m_handle, node.node(), &res);
    if (nErr < 0) {
        qWarning() << "screenshot raw failed";
        return QImage();
    }

    Q_ASSERT(res.format == MPV_FORMAT_NODE_MAP);

    int w = 0, h = 0, stride = 0;

    mpv_node_list *pNodeList = res.u.list;
    uchar *pData = nullptr;

    for (int n = 0; n < pNodeList->num; n++) {
        auto key = QString::fromUtf8(pNodeList->keys[n]);
        if (key == "w") {
            w = static_cast<int>(pNodeList->values[n].u.int64);
        } else if (key == "h") {
            h = static_cast<int>(pNodeList->values[n].u.int64);
        } else if (key == "stride") {
            stride = static_cast<int>(pNodeList->values[n].u.int64);
        } else if (key == "format") {
            auto format = QString::fromUtf8(pNodeList->values[n].u.string);
            qInfo() << "format" << format;
        } else if (key == "data") {
            pData = static_cast<uchar *>(pNodeList->values[n].u.ba->data);
        }
    }

    if (pData) {
        //alpha should be ignored
        auto img = QImage(static_cast<const uchar *>(pData), w, h, stride, QImage::Format_RGB32);
        img.bits();
        int rotationdegree = videoRotation();
        if (rotationdegree && (DCompositeManager::get().composited() || bNeedRotate)) {      //只有opengl窗口需要自己旋转
            QMatrix matrix;
            matrix.rotate(rotationdegree);
            img = QPixmap::fromImage(img).transformed(matrix, Qt::SmoothTransformation).toImage();
        }
        m_freeNodecontents(&res);
        return img;
    }

    m_freeNodecontents(&res);
    qInfo() << "failed";
    return QImage();
}

void DMpvProxy::stepBurstScreenshot()
{
    if (!m_bInBurstShotting) {
        return;
    }

    auto pos = nextBurstShootPoint();
    my_command(m_handle, QList<QVariant> {"seek", pos, "absolute"});
//    int tries = 10;
    while (true) {
        mpv_event *pEvent = m_waitEvent(m_handle, 0.005);
        if (pEvent->event_id == MPV_EVENT_NONE)
            continue;

        if (pEvent->event_id == MPV_EVENT_PLAYBACK_RESTART) {
            qInfo() << "seek finished" << elapsed();
            break;
        }

        if (pEvent->event_id == MPV_EVENT_END_FILE) {
            qInfo() << "seek finished (end of file)" << elapsed();
            break;
        }
    }

    QImage img = takeOneScreenshot();
    if (img.isNull()) {
        emit notifyScreenshot(img, elapsed());
        stopBurstScreenshot();
        return;
    }
    emit notifyScreenshot(img, elapsed());
    QTimer::singleShot(0, this, &DMpvProxy::stepBurstScreenshot);
}

void DMpvProxy::stopBurstScreenshot()
{
    m_bInBurstShotting = false;
    //my_command(m_handle, QList<QVariant> {"revert-seek", "mark"});
    my_set_property(m_handle, "time-pos", m_posBeforeBurst);
}

void DMpvProxy::seekForward(int nSecs)
{
    if (state() == PlayState::Stopped) return;

    if (m_bPendingSeek) return;
    QList<QVariant> listArgs = { "seek", QVariant(nSecs), "relative+exact" };
    qInfo() << listArgs;
    my_command_async(m_handle, listArgs, AsyncReplyTag::SEEK);
    m_bPendingSeek = true;
}

void DMpvProxy::seekBackward(int nSecs)
{
    if (state() == PlayState::Stopped) return;

    if (m_bPendingSeek) return;
    if (nSecs > 0)
        nSecs = -nSecs;
    QList<QVariant> listArgs = { "seek", QVariant(nSecs), "relative+exact" };
    qInfo() << listArgs;
    my_command_async(m_handle, listArgs, AsyncReplyTag::SEEK);
    m_bPendingSeek = true;
}

void DMpvProxy::seekAbsolute(int nPos)
{
    if (state() == PlayState::Stopped) return;

    if (m_bPendingSeek) return;
    QList<QVariant> listArgs = { "seek", QVariant(nPos), "absolute" };
    qInfo() << listArgs;
    //command(m_handle, args);
    m_bPendingSeek = true;
    my_command_async(m_handle, listArgs, AsyncReplyTag::SEEK);
}

QSize DMpvProxy::videoSize() const
{
    if (state() == PlayState::Stopped) return QSize(-1, -1);
    QSize size = QSize(my_get_property(m_handle, "dwidth").toInt(),
                       my_get_property(m_handle, "dheight").toInt());

    auto r = my_get_property(m_handle, "video-out-params/rotate").toInt();
    if (r == 90 || r == 270) {
        size.transpose();
    }

    return size;
}

qint64 DMpvProxy::duration() const
{
    bool bRawFormat = false;

//    if (0 < dynamic_cast<PlayerEngine *>(m_pParentWidget)->getplaylist()->size()) {
//        PlayItemInfo currentInfo = dynamic_cast<PlayerEngine *>(m_pParentWidget)->getplaylist()->currentInfo();
//        bRawFormat = currentInfo.mi.isRawFormat();
//    }

    if (bRawFormat) {     // 因为格式众多时长输出不同，这里做统一处理不显示时长
        return 0;
    } else {
        return my_get_property(m_handle, "duration").value<qint64>();
    }
}


qint64 DMpvProxy::elapsed() const
{
    if (state() == PlayState::Stopped) return 0;
    return  my_get_property(m_handle, "time-pos").value<qint64>();

}

void DMpvProxy::updatePlayingMovieInfo()
{
    m_movieInfo.subs.clear();
    m_movieInfo.audios.clear();

    QList<QVariant> listInfo = my_get_property(m_handle, "track-list").toList();
    auto p = listInfo.begin();
    while (p != listInfo.end()) {
        const auto &t = p->toMap();
        if (t["type"] == "audio") {
            AudioInfo audioInfo;
            audioInfo["type"] = t["type"];
            audioInfo["id"] = t["id"];
            audioInfo["lang"] = t["lang"];
            audioInfo["external"] = t["external"];
            audioInfo["external-filename"] = t["external-filename"];
            audioInfo["selected"] = t["selected"];
            audioInfo["title"] = t["title"];

            if (t["title"].toString().size() == 0) {
                if (t["lang"].isValid() && t["lang"].toString().size() && t["lang"].toString() != "und")
                    audioInfo["title"] = t["lang"];
                else if (!t["external"].toBool())
                    audioInfo["title"] = "[internal]";
            }


            m_movieInfo.audios.append(audioInfo);
        } else if (t["type"] == "sub") {
            SubtitleInfo titleInfo;
            titleInfo["type"] = t["type"];
            titleInfo["id"] = t["id"];
            titleInfo["lang"] = t["lang"];
            titleInfo["external"] = t["external"];
            titleInfo["external-filename"] = t["external-filename"];
            titleInfo["selected"] = t["selected"];
            titleInfo["title"] = t["title"];
            if (t["title"].toString().size() == 0) {
                if (t["lang"].isValid() && t["lang"].toString().size() && t["lang"].toString() != "und")
                    titleInfo["title"] = t["lang"];
                else if (!t["external"].toBool())
                    titleInfo["title"] = tr("Internal");
            }
            m_movieInfo.subs.append(titleInfo);
        }
        ++p;
    }

    qInfo() << m_movieInfo.subs;
    qInfo() << m_movieInfo.audios;
}

void DMpvProxy::nextFrame()
{
    if (state() == PlayState::Stopped) return;

    QList<QVariant> listArgs = { "frame-step"};
    my_command(m_handle, listArgs);
}

void DMpvProxy::previousFrame()
{
    if (state() == PlayState::Stopped) return;

    QList<QVariant> listArgs = { "frame-back-step"};
    my_command(m_handle, listArgs);
}

void DMpvProxy::changehwaccelMode(hwaccelMode hwaccelMode)
{
    switch (hwaccelMode) {
    case hwaccelAuto:
        m_bHwaccelAuto = true;
        break;
    case hwaccelOpen:
        m_bHwaccelAuto = false;
        my_set_property(m_handle, "hwdec", "auto");
        break;
    case hwaccelClose:
        m_bHwaccelAuto = false;
        my_set_property(m_handle, "hwdec", "off");
        break;
    }
}

void DMpvProxy::makeCurrent()
{
//    m_pMpvGLwidget->makeCurrent();
}

QVariant DMpvProxy::getProperty(const QString &sName)
{
    return my_get_property(m_handle, sName.toUtf8().data());
}

void DMpvProxy::setProperty(const QString &sName, const QVariant &val)
{
    if (sName == "pause-on-start") {
        m_bPauseOnStart = val.toBool();
    } else if (sName == "video-zoom") {
        my_set_property(m_handle, sName, val.toDouble());
    } else {
        my_set_property(m_handle, sName.toUtf8().data(), val);
    }
}

DMULTIMEDIA_END_NAMESPACE // end of namespace

