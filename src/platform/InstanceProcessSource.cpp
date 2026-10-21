#include "InstanceProcessSource.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <tlhelp32.h>
#endif
namespace studio {
RunningInstanceResult InstanceProcessSource::scan(const QVariantList &packs) const {
#ifdef Q_OS_WIN
    if (packs.isEmpty())
        return {};
    QMutexLocker lock(&m_scanMutex);
    QStringList fingerprint;
    bool cacheable = true;
    const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return {{}, "Could not read running instance status."};
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry))
        do {
            const auto name = QString::fromWCharArray(entry.szExeFile);
            if (name.compare("java.exe", Qt::CaseInsensitive) == 0 ||
                name.compare("javaw.exe", Qt::CaseInsensitive) == 0) {
                FILETIME created{}, exited{}, kernel{}, user{};
                const auto process =
                    OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
                if (process && GetProcessTimes(process, &created, &exited, &kernel, &user))
                    fingerprint << QString::number(entry.th32ProcessID) + ':' +
                                       QString::number((quint64(created.dwHighDateTime) << 32) |
                                                       created.dwLowDateTime);
                else {
                    cacheable = false;
                    fingerprint << QString::number(entry.th32ProcessID);
                }
                if (process)
                    CloseHandle(process);
            }
        } while (Process32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    if (fingerprint.isEmpty()) {
        m_processFingerprint.clear();
        m_processDirectories = {};
        return {};
    }
    fingerprint.sort();
    if (!cacheable || fingerprint != m_processFingerprint) {
        // Command lines stay inside the helper. Only game directories and PIDs leave it;
        // access tokens/user arguments are never returned, logged or retained.
        const QString script = QStringLiteral(R"PS(
$ErrorActionPreference='Stop'
[Console]::OutputEncoding=[System.Text.UTF8Encoding]::new($false)
try {
 $rows=@(Get-CimInstance Win32_Process -Filter "Name='java.exe' OR Name='javaw.exe'" | ForEach-Object {
  $match=[regex]::Match($_.CommandLine,'(?i)(?:^|\s)--gameDir(?:\s+|=)(?:"([^"]+)"|([^\s]+))')
  if($match.Success){$directory=$match.Groups[1].Value;if(!$directory){$directory=$match.Groups[2].Value};[pscustomobject]@{pid=$_.ProcessId;directory=$directory}}
 })
 ConvertTo-Json -InputObject $rows -Compress
} catch { [Console]::Error.WriteLine('Could not inspect running Minecraft instances.');exit 1 }
)PS");
        QProcess process;
        process.setCreateProcessArgumentsModifier(
            [](QProcess::CreateProcessArguments *args) { args->flags |= CREATE_NO_WINDOW; });
        process.start("powershell.exe",
                      {"-NoProfile", "-NonInteractive", "-WindowStyle", "Hidden", "-Command", script});
        if (!process.waitForFinished(5000)) {
            process.kill();
            process.waitForFinished(1000);
            return {{}, "Running instance detection timed out."};
        }
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
            return {{}, "Running instance status is unavailable."};
        const auto bytes = process.readAllStandardOutput();
        if (bytes.size() > 65536)
            return {{}, "Running instance response was too large."};
        const auto document = QJsonDocument::fromJson(bytes);
        if (!document.isArray())
            return {{}, "Could not read running instance status."};
        m_processDirectories = document.array();
        m_processFingerprint = cacheable ? fingerprint : QStringList{};
    }
    QVariantList running;
    for (const auto &value : m_processDirectories) {
        const auto record = value.toObject();
        const auto directory = QFileInfo(record.value("directory").toString()).canonicalFilePath();
        if (directory.isEmpty())
            continue;
        for (const auto &item : packs) {
            auto pack = item.toMap();
            if (QFileInfo(pack.value("path").toString())
                    .canonicalFilePath()
                    .compare(directory, Qt::CaseInsensitive) == 0) {
                pack["pid"] = record.value("pid").toInteger();
                running << pack;
                break;
            }
        }
    }
    return {running, {}};
#else
    Q_UNUSED(packs)
    return {{}, "Running instance detection is unavailable on this platform."};
#endif
}
} // namespace studio
