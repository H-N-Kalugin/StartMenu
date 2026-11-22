#include "AudioControl.h"

#include <windows.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <propidl.h>
#include <propsys.h>

namespace
{
    // Имя устройства (PKEY_Device_FriendlyName) — объявлено здесь, чтобы не тянуть отдельную библиотеку с ключами.
    const PROPERTYKEY kFriendlyName = {{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};

    // Недокументированный, но много лет стабильный интерфейс Windows для смены устройства по умолчанию
    // (им пользуются все переключатели звука). Объявляем только нужную часть таблицы методов.
    struct __declspec(uuid("f8679f50-850a-41cf-9c72-430f290290c8")) IPolicyConfig : public IUnknown
    {
        virtual HRESULT STDMETHODCALLTYPE GetMixFormat(PCWSTR, void**) = 0;
        virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(PCWSTR, INT, void**) = 0;
        virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(PCWSTR) = 0;
        virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(PCWSTR, void*, void*) = 0;
        virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(PCWSTR, INT, PINT64, PINT64) = 0;
        virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(PCWSTR, PINT64) = 0;
        virtual HRESULT STDMETHODCALLTYPE GetShareMode(PCWSTR, void*) = 0;
        virtual HRESULT STDMETHODCALLTYPE SetShareMode(PCWSTR, void*) = 0;
        virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
        virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
        virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(PCWSTR id, ERole role) = 0;
        virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(PCWSTR, INT) = 0;
    };
    const CLSID kPolicyConfigClient = {0x870af99c, 0x171d, 0x4f9e, {0xaf, 0x0d, 0xe6, 0x3d, 0xf4, 0x0c, 0x2b, 0xc9}};

    template <class T>
    struct Com
    {
        T* p = nullptr;
        ~Com() { if (p) p->Release(); }
        T** operator&() { return &p; }
        T* operator->() { return p; }
        explicit operator bool() const { return p != nullptr; }
    };

    bool enumerator(Com<IMMDeviceEnumerator>& en)
    {
        return SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en)));
    }

    bool endpointVolume(Com<IAudioEndpointVolume>& vol)
    {
        Com<IMMDeviceEnumerator> en;
        Com<IMMDevice> dev;
        if (!enumerator(en) || FAILED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev)))
            return false;
        return SUCCEEDED(dev->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&vol)));
    }
}

namespace Audio
{
QList<Device> outputs()
{
    QList<Device> out;
    Com<IMMDeviceEnumerator> en;
    if (!enumerator(en))
        return out;
    QString defId;
    {
        Com<IMMDevice> def;
        LPWSTR id = nullptr;
        if (SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eConsole, &def)) && SUCCEEDED(def->GetId(&id)))
        {
            defId = QString::fromWCharArray(id);
            CoTaskMemFree(id);
        }
    }
    Com<IMMDeviceCollection> col;
    if (FAILED(en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &col)))
        return out;
    UINT n = 0;
    col->GetCount(&n);
    for (UINT i = 0; i < n; ++i)
    {
        Com<IMMDevice> dev;
        if (FAILED(col->Item(i, &dev)))
            continue;
        Device d;
        LPWSTR id = nullptr;
        if (SUCCEEDED(dev->GetId(&id)))
        {
            d.id = QString::fromWCharArray(id);
            CoTaskMemFree(id);
        }
        Com<IPropertyStore> props;
        if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &props)))
        {
            PROPVARIANT v;
            PropVariantInit(&v);
            if (SUCCEEDED(props->GetValue(kFriendlyName, &v)) && v.vt == VT_LPWSTR)
                d.name = QString::fromWCharArray(v.pwszVal);
            PropVariantClear(&v);
        }
        d.isDefault = d.id == defId;
        out.append(d);
    }
    return out;
}

bool setDefault(const QString& id)
{
    Com<IPolicyConfig> pc;
    if (FAILED(CoCreateInstance(kPolicyConfigClient, nullptr, CLSCTX_ALL, __uuidof(IPolicyConfig), reinterpret_cast<void**>(&pc))))
        return false;
    const std::wstring w = id.toStdWString();
    bool ok = true;
    for (ERole role : {eConsole, eMultimedia, eCommunications})
        ok = SUCCEEDED(pc->SetDefaultEndpoint(w.c_str(), role)) && ok;
    return ok;
}

float volume()
{
    Com<IAudioEndpointVolume> vol;
    float v = 0;
    if (endpointVolume(vol))
        vol->GetMasterVolumeLevelScalar(&v);
    return v;
}

void setVolume(float v)
{
    Com<IAudioEndpointVolume> vol;
    if (endpointVolume(vol))
        vol->SetMasterVolumeLevelScalar(qBound(0.0f, v, 1.0f), nullptr);
}

bool muted()
{
    Com<IAudioEndpointVolume> vol;
    BOOL m = FALSE;
    if (endpointVolume(vol))
        vol->GetMute(&m);
    return m;
}

void setMuted(bool on)
{
    Com<IAudioEndpointVolume> vol;
    if (endpointVolume(vol))
        vol->SetMute(on, nullptr);
}
}
