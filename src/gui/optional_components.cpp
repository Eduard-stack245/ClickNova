// SPDX-License-Identifier: Apache-2.0
#include "optional_components.hpp"
#include "portable_storage.hpp"
#include <QApplication>
#include <QCheckBox>
#include <QCryptographicHash>
#include <QTextStream>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace clicknova::gui::components {
namespace {
constexpr qint64 kMaxModelBytes = 30 * 1024 * 1024;
QString settingsKey() { return QStringLiteral("optionalComponents/firstRunAnswered"); }
QString languageDirectory() { return portable::filePath(QStringLiteral("tessdata")); }
QString displayStatus() {
#ifdef CLICKNOVA_WITH_OPENCV
    const QString cvStatus = QStringLiteral("OpenCV: встроен в эту сборку");
#else
    const QString cvStatus = QStringLiteral("OpenCV: не включён в эту сборку");
#endif
#ifdef CLICKNOVA_WITH_TESSERACT
    const QString ocrStatus = QStringLiteral("Tesseract OCR: встроен в эту сборку");
#else
    const QString ocrStatus = QStringLiteral("Tesseract OCR: не включён в эту сборку");
#endif
    return cvStatus + QStringLiteral("\n") + ocrStatus + QStringLiteral("\n") +
        QStringLiteral("Языки OCR: английский — %1, русский — %2\nДанные: %3")
            .arg(QFileInfo(QDir(languageDirectory()).filePath(QStringLiteral("eng.traineddata"))).exists()
                     ? QStringLiteral("установлен") : QStringLiteral("не установлен"),
                 QFileInfo(QDir(languageDirectory()).filePath(QStringLiteral("rus.traineddata"))).exists()
                     ? QStringLiteral("установлен") : QStringLiteral("не установлен"),
                 portable::dataRoot());
}
QString buildScript() {
#ifdef _WIN32
    QDir dir(QCoreApplication::applicationDirPath());
    // Developer checkout or source distribution; don't rely on C:/D: or cwd.
    for (int depth = 0; depth < 5; ++depth) {
        const QString path = dir.filePath(QStringLiteral("scripts/visual-recognition-installer.ps1"));
        if (QFileInfo::exists(path) && QFileInfo::exists(dir.filePath(QStringLiteral("CMakeLists.txt"))))
            return QDir::cleanPath(path);
        if (!dir.cdUp()) break;
    }
#endif
    return {};
}

// Record ONLY downloads that did not exist before ClickNova created them.
// This ledger belongs to the portable data folder and is not a machine-wide
// registry of installed packages. Uninstaller checks SHA-256 before deleting.
void recordOwnedModel(const QString& fileName, const QString& name) {
    QFile model(fileName);
    if (!model.open(QIODevice::ReadOnly)) return;
    const QString checksum = QString::fromLatin1(
        QCryptographicHash::hash(model.readAll(), QCryptographicHash::Sha256).toHex());
    const QString ledger = portable::filePath(QStringLiteral("owned-components.tsv"));
    QFile existing(ledger);
    QStringList records;
    if (existing.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString old = QString::fromUtf8(existing.readAll());
        for (const auto& line : old.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            if (!line.endsWith(QLatin1Char('\t') + QStringLiteral("tessdata/") + name + QStringLiteral(".traineddata")))
                records.append(line);
        }
    }
    records.append(checksum + QLatin1Char('\t') + QStringLiteral("tessdata/") + name + QStringLiteral(".traineddata"));
    QSaveFile update(ledger);
    if (update.open(QIODevice::WriteOnly)) {
        update.write((records.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8());
        update.commit();
    }
}

bool downloadModel(const QString& name, QWidget* parent) {
    const QUrl url(QStringLiteral("https://raw.githubusercontent.com/tesseract-ocr/tessdata_fast/main/") +
                   name + QStringLiteral(".traineddata"));
    if (!QDir().mkpath(languageDirectory())) {
        QMessageBox::warning(parent, QStringLiteral("ClickNova"),
            QStringLiteral("Не удалось создать папку для языков OCR."));
        return false;
    }
    const QString output = QDir(languageDirectory()).filePath(name + QStringLiteral(".traineddata"));
    // Do not overwrite or acquire ownership of pre-existing OCR models.
    if (QFileInfo::exists(output) || QFileInfo(output).isSymLink()) {
        QMessageBox::information(parent, QStringLiteral("ClickNova"),
            QStringLiteral("Модель %1 уже существует. Файл сохранён без изменений; ClickNova не будет считать его своей загрузкой.").arg(name));
        return true;
    }
    QSaveFile file(output);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(parent, QStringLiteral("ClickNova"), file.errorString());
        return false;
    }
    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setRawHeader("User-Agent", "ClickNova/0.6.0");
    QNetworkReply* reply = manager.get(request);
    QProgressDialog progress(QStringLiteral("Загрузка %1 (официальный репозиторий Tesseract OCR)…").arg(name),
                             QStringLiteral("Отмена"), 0, 100, parent);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    bool oversized = false;
    qint64 bytes = 0;
    auto consume = [&] {
        const QByteArray chunk = reply->readAll();
        bytes += chunk.size();
        if (bytes > kMaxModelBytes || file.write(chunk) != chunk.size()) {
            oversized = true;
            reply->abort();
        }
    };
    QObject::connect(reply, &QIODevice::readyRead, &progress, consume);
    QObject::connect(reply, &QNetworkReply::downloadProgress, &progress,
                     [&progress](qint64 received, qint64 total) {
        if (total > 0) progress.setValue(static_cast<int>(qBound<qint64>(qint64{0}, received * 100 / total, qint64{99})));
    });
    QObject::connect(&progress, &QProgressDialog::canceled, reply, &QNetworkReply::abort);
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, reply, &QNetworkReply::abort);
    timeout.start(120000);
    // Nested event loop is limited to this explicit modal download operation.
    QEventLoop wait;
    QObject::connect(reply, &QNetworkReply::finished, &wait, &QEventLoop::quit);
    if (!reply->isFinished()) wait.exec();
    consume();
    progress.setValue(100);
    // A file may have appeared during the download (another process/user).
    // Never replace it or mark it as owned by ClickNova.
    if (QFileInfo::exists(output) || QFileInfo(output).isSymLink()) {
        file.cancelWriting();
        reply->deleteLater();
        QMessageBox::information(parent, QStringLiteral("ClickNova"),
            QStringLiteral("Файл %1 уже создан другим приложением и оставлен без изменений.").arg(name));
        return true;
    }
    const bool success = reply->error() == QNetworkReply::NoError &&
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200 &&
        !oversized && bytes > 100000 && file.commit();
    if (!success) {
        const QString why = oversized ? QStringLiteral("Файл превышает допустимый размер или ошибка записи.") : reply->errorString();
        file.cancelWriting();
        QMessageBox::warning(parent, QStringLiteral("ClickNova"),
                             QStringLiteral("Не удалось загрузить %1: %2").arg(name, why));
    }
    reply->deleteLater();
    if (success) recordOwnedModel(output, name);
    return success;
}

void installModels(QWidget* parent, QLabel* status) {
    QMessageBox confirmation(parent);
    confirmation.setWindowTitle(QStringLiteral("Дополнительные файлы ClickNova"));
    confirmation.setText(QStringLiteral("Скачать языковые модели OCR (английский и русский)?"));
    confirmation.setInformativeText(QStringLiteral(
        "Будут загружены eng.traineddata и rus.traineddata из официального репозитория "
        "Tesseract (GitHub) в локальную папку data/tessdata. Загрузка выполняется только "
        "с вашего согласия. Это НЕ устанавливает сам OpenCV или Tesseract."));
    confirmation.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    confirmation.setDefaultButton(QMessageBox::No);
    if (confirmation.exec() != QMessageBox::Yes) return;
    const bool en = downloadModel(QStringLiteral("eng"), parent);
    const bool ru = en && downloadModel(QStringLiteral("rus"), parent);
    if (status) status->setText(displayStatus());
    if (en && ru) QMessageBox::information(parent, QStringLiteral("ClickNova"),
        QStringLiteral("Языковые файлы OCR установлены. Перезапустите ClickNova для их использования."));
}

void installEngine(QWidget* parent) {
    const QString script = buildScript();
    if (script.isEmpty()) {
        QMessageBox::information(parent, QStringLiteral("ClickNova"), QStringLiteral(
            "В этой переносимой сборке нет установщика движка распознавания. "
            "Для OpenCV/OCR требуется отдельная сборка ClickNova с библиотеками. "
            "Скачивание языковых моделей не включает движок. "
            "Установите соответствующую сборку для Windows или Linux; "
            "для исходного проекта используйте скрипт Enable-OpenCV-OCR.cmd."));
        return;
    }
    const auto result = QMessageBox::question(parent, QStringLiteral("ClickNova"), QStringLiteral(
        "Откроется установщик с отображением этапов и журналом. Он проверит vcpkg, "
        "при необходимости скачает исправную отдельную копию, установит OpenCV/Tesseract "
        "и языки OCR, затем пересоберёт ClickNova и проверит тесты. "
        "Для исходного проекта нужны Qt, CMake, MSVC и интернет. "
        "Это не загрузка готового модуля. Закрыть ClickNova и начать?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (result != QMessageBox::Yes) return;
    bool ok = QProcess::startDetached(QStringLiteral("powershell.exe"),
        {QStringLiteral("-NoProfile"), QStringLiteral("-WindowStyle"),
         QStringLiteral("Hidden"), QStringLiteral("-ExecutionPolicy"),
         QStringLiteral("Bypass"), QStringLiteral("-File"), script,
         QStringLiteral("-WaitForApplicationExit")}, QFileInfo(script).absolutePath());
    if (ok) QTimer::singleShot(0, qApp, [] { QCoreApplication::quit(); });
    else QMessageBox::warning(parent, QStringLiteral("ClickNova"), QStringLiteral("Не удалось запустить установщик."));
}
}

// On Windows the running EXE cannot remove itself. A packaged standalone
// script waits for ClickNova to exit. Developer/source builds have no package
// manifest and must never automatically delete their source tree or vcpkg.
void requestUninstall(QWidget* parent) {
    QDir appDir(QCoreApplication::applicationDirPath());
#ifdef __linux__
    // A running AppImage is mounted under /tmp. Its uninstaller belongs next
    // to the original file, not the transient AppDir. This exists only for
    // the ownership-tracked portable ZIP, never for unrelated AppImages.
    const QString appImagePath = QFile::decodeName(qgetenv("APPIMAGE"));
    const QFileInfo imageFile(appImagePath);
    if (imageFile.isFile() && !imageFile.isSymLink()) {
        const QDir containing(imageFile.absolutePath());
        if (QFileInfo::exists(containing.filePath(QStringLiteral("install-manifest.tsv"))) &&
            QFileInfo::exists(containing.filePath(QStringLiteral("uninstall-clicknova.sh"))))
            appDir = containing;
    }
#endif
    const QString manifest = appDir.filePath(QStringLiteral("install-manifest.tsv"));
#ifdef _WIN32
    const QString script = appDir.filePath(QStringLiteral("uninstall-clicknova.ps1"));
#else
    const QString script = appDir.filePath(QStringLiteral("uninstall-clicknova.sh"));
#endif
    if (!QFileInfo::exists(manifest) || !QFileInfo::exists(script)) {
        QMessageBox::information(parent, QStringLiteral("Удаление ClickNova"),
            QStringLiteral("Это исходная/разработческая сборка или пакет без манифеста. "
                           "Автоматическое удаление запрещено, чтобы не удалить чужие файлы. "
                           "Для безопасного удаления установите готовый portable-пакет ClickNova."));
        return;
    }
    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("portableUninstallDialog"));
    dialog.setWindowTitle(QStringLiteral("Удалить ClickNova"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* intro = new QLabel(QStringLiteral(
        "Программа и её собственные файлы будут удалены. Системные библиотеки, "
        "общий vcpkg, ранее существовавшие файлы и другие программы не затрагиваются. "
        "Отметьте дополнительные данные, которые тоже нужно удалить:"));
    intro->setWordWrap(true);
    layout->addWidget(intro);
    auto* models = new QCheckBox(QStringLiteral("Файлы OCR, скачанные именно ClickNova (не сторонние)"));
    auto* settings = new QCheckBox(QStringLiteral("Настройки программы"));
    auto* scenarios = new QCheckBox(QStringLiteral("Сценарии и автосохранение"));
    auto* recovery = new QCheckBox(QStringLiteral("Черновики восстановления записи"));
    auto* logs = new QCheckBox(QStringLiteral("Журналы работы"));
    for (auto* item : {models, settings, scenarios, recovery, logs}) layout->addWidget(item);
    auto* path = new QLabel(QStringLiteral("Файлы пользователя: %1\nНевыбранные данные сохранятся.").arg(portable::dataRoot()));
    path->setWordWrap(true);
    layout->addWidget(path);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    auto* remove = buttons->addButton(QStringLiteral("Удалить ClickNova…"), QDialogButtonBox::DestructiveRole);
    QObject::connect(remove, &QPushButton::clicked, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) return;
    if (QMessageBox::warning(parent, QStringLiteral("Подтверждение удаления"),
        QStringLiteral("Удалить portable-приложение ClickNova? Выбранные категории данных "
                       "тоже будут удалены, если их файлы доступны. Это действие нельзя отменить."),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
#ifdef _WIN32
    QStringList args{QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"),
        QStringLiteral("Bypass"), QStringLiteral("-File"), script,
        QStringLiteral("-Confirmed"), QStringLiteral("-ParentPid"), QString::number(QCoreApplication::applicationPid()),
        QStringLiteral("-DataRoot"), portable::dataRoot()};
    if (models->isChecked()) args << QStringLiteral("-RemoveComponents");
    if (settings->isChecked()) args << QStringLiteral("-RemoveSettings");
    if (scenarios->isChecked()) args << QStringLiteral("-RemoveScenarios");
    if (recovery->isChecked()) args << QStringLiteral("-RemoveRecovery");
    if (logs->isChecked()) args << QStringLiteral("-RemoveLogs");
    const bool launched = QProcess::startDetached(QStringLiteral("powershell.exe"), args, appDir.absolutePath());
#else
    QStringList args{script, QStringLiteral("--confirmed"),
        QStringLiteral("--parent-pid"), QString::number(QCoreApplication::applicationPid()),
        QStringLiteral("--data-dir"), portable::dataRoot()};
    if (models->isChecked()) args << QStringLiteral("--remove-components");
    if (settings->isChecked()) args << QStringLiteral("--remove-settings");
    if (scenarios->isChecked()) args << QStringLiteral("--remove-scenarios");
    if (recovery->isChecked()) args << QStringLiteral("--remove-recovery");
    if (logs->isChecked()) args << QStringLiteral("--remove-logs");
    const bool launched = QProcess::startDetached(QStringLiteral("/usr/bin/env"),
        QStringList{QStringLiteral("bash")} + args, appDir.absolutePath());
#endif
    if (!launched) {
        QMessageBox::warning(parent, QStringLiteral("ClickNova"),
                             QStringLiteral("Не удалось запустить безопасное удаление. Файлы не удалены."));
        return;
    }
    // Avoid writing settings when exiting for removal.
    QTimer::singleShot(0, qApp, [] { QCoreApplication::quit(); });
}

QWidget* createSettingsPanel(QWidget* parent) {
    auto* page = new QWidget(parent);
    page->setObjectName(QStringLiteral("optionalComponentsSettings"));
    auto* layout = new QVBoxLayout(page);
    auto* intro = new QLabel(QStringLiteral(
        "Дополнительные компоненты необязательны. Без них ClickNova продолжает работать "
        "с обычными шагами и доступными API приложений. OpenCV распознаёт изображения, "
        "Tesseract OCR — текст. Обработка происходит локально."));
    intro->setWordWrap(true);
    layout->addWidget(intro);
    auto* status = new QLabel(displayStatus());
    status->setObjectName(QStringLiteral("optionalComponentsStatus"));
    status->setWordWrap(true);
    layout->addWidget(status);
    auto* row = new QHBoxLayout;
    auto* models = new QPushButton(QStringLiteral("Скачать языки OCR"));
    models->setObjectName(QStringLiteral("installOcrLanguages"));
    row->addWidget(models);
    QObject::connect(models, &QPushButton::clicked, page, [page, status] { installModels(page, status); });
    auto* engine = new QPushButton(QStringLiteral("Собрать с OpenCV / Tesseract (для разработчиков)"));
    engine->setObjectName(QStringLiteral("installVisualEngine"));
    row->addWidget(engine);
    QObject::connect(engine, &QPushButton::clicked, page, [page] { installEngine(page); });
    layout->addLayout(row);
    auto* refresh = new QPushButton(QStringLiteral("Обновить состояние"));
    refresh->setObjectName(QStringLiteral("refreshOptionalComponents"));
    QObject::connect(refresh, &QPushButton::clicked, page, [status] { status->setText(displayStatus()); });
    layout->addWidget(refresh);
    auto* hint = new QLabel(QStringLiteral(
        "Переносимый режим: настройки, сценарии и файлы OCR сохраняются в папке data "
        "рядом с приложением. На Linux требуется отдельная Linux-сборка: Windows EXE там не запускается."));
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto* uninstallButton = new QPushButton(QStringLiteral("Удалить ClickNova…"));
    uninstallButton->setObjectName(QStringLiteral("uninstallClickNova"));
    QObject::connect(uninstallButton, &QPushButton::clicked, page,
                     [page] { requestUninstall(page); });
    layout->addWidget(uninstallButton);
    layout->addStretch();
    return page;
}

void showFirstRun(QWidget* parent) {
    QSettings settings(QStringLiteral("ClickNova"), QStringLiteral("ClickNova"));
    if (settings.value(settingsKey(), false).toBool()) return;
    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("firstRunOptionalComponents"));
    dialog.setWindowTitle(QStringLiteral("Первый запуск ClickNova — дополнительные компоненты"));
    dialog.resize(580, 380);
    auto* layout = new QVBoxLayout(&dialog);
    auto* header = new QLabel(QStringLiteral(
        "ClickNova может работать без дополнительных файлов. Хотите установить компоненты "
        "визуального распознавания?"));
    header->setWordWrap(true);
    layout->addWidget(header);
    auto* description = new QLabel(QStringLiteral(
        "• OpenCV — поиск кнопок/объектов по изображению (включается при сборке приложения).\n"
        "• Tesseract — OCR: поиск текста на экране (включается при сборке).\n"
        "• Английская и русская языковые модели — загружаемые файлы OCR.\n\n"
        "Ничего не скачивается автоматически. Можно отказаться и установить позднее "
        "в Настройки → Компоненты. Для установки движка в исходной сборке нужна среда разработки."));
    description->setWordWrap(true);
    layout->addWidget(description);
    auto* status = new QLabel(displayStatus());
    status->setWordWrap(true);
    layout->addWidget(status);
    auto* models = new QPushButton(QStringLiteral("Скачать языки OCR (русский и английский)"));
    QObject::connect(models, &QPushButton::clicked, &dialog, [&dialog, status] {
        installModels(&dialog, status);
    });
    layout->addWidget(models);
    auto* engine = new QPushButton(QStringLiteral("Собрать с OpenCV / Tesseract (для разработчиков)"));
    QObject::connect(engine, &QPushButton::clicked, &dialog, [&dialog] { installEngine(&dialog); });
    layout->addWidget(engine);
    auto* buttons = new QDialogButtonBox(&dialog);
    auto* later = buttons->addButton(QStringLiteral("Напомнить при следующем запуске"), QDialogButtonBox::AcceptRole);
    auto* skip = buttons->addButton(QStringLiteral("Продолжить без загрузки"), QDialogButtonBox::RejectRole);
    QObject::connect(later, &QPushButton::clicked, &dialog, &QDialog::accept);
    QObject::connect(skip, &QPushButton::clicked, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    const int result = dialog.exec();
    // "Remind me" shows the explanation again on next startup; opting out
    // still leaves all component actions available in Settings.
    settings.setValue(settingsKey(), result != QDialog::Accepted);
    settings.sync();
}
}
