// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "modelsettings.h"

#include "core/claude.h"
#include "core/embedding.h"
#include "core/ollama.h"
#include "core/settings.h"

#include "secrets.h"

#include <KLocalizedString>

#include <QQmlEngine>

namespace
{
ModelSettings *s_instance = nullptr;

// the Anthropic API key, in the keyring
Secrets::Attributes claudeKeyAttributes()
{
    return {{QStringLiteral("application"), QStringLiteral("wayland95-athenaeum")}, {QStringLiteral("service"), QStringLiteral("anthropic")}};
}
}

ModelSettings *ModelSettings::createInstance(QObject *parent)
{
    Q_ASSERT(!s_instance);
    s_instance = new ModelSettings(parent);
    return s_instance;
}

ModelSettings *ModelSettings::instance()
{
    return s_instance;
}

ModelSettings *ModelSettings::create(QQmlEngine *, QJSEngine *)
{
    QQmlEngine::setObjectOwnership(s_instance, QQmlEngine::CppOwnership);
    return s_instance;
}

ModelSettings::ModelSettings(QObject *parent)
    : QObject(parent)
    , m_client(new OllamaClient(this))
{
    m_client->setBaseUrl(url());
    loadClaudeKey();
}

void ModelSettings::loadClaudeKey()
{
    // ANTHROPIC_API_KEY, if set, until the keyring answers (a key kept there wins)
    m_client->setClaudeKey(qEnvironmentVariable("ANTHROPIC_API_KEY"));
    if (!Secrets::available())
        return;
    Secrets::lookup(claudeKeyAttributes(), [this](bool found, const QString &key) {
        if (found && !key.trimmed().isEmpty()) {
            m_client->setClaudeKey(key);
            Q_EMIT claudeKeyChanged();
            Q_EMIT statusChanged();
        }
    });
}

bool ModelSettings::keyringAvailable() const
{
    return Secrets::available();
}

bool ModelSettings::claudeKeySet() const
{
    return m_client->hasClaudeKey();
}

void ModelSettings::setClaudeKey(const QString &key)
{
    const QString k = key.trimmed();
    m_claudeKeyError.clear();
    if (k.isEmpty()) {
        Secrets::remove(claudeKeyAttributes());
        m_client->setClaudeKey(qEnvironmentVariable("ANTHROPIC_API_KEY"));
        Q_EMIT claudeKeyChanged();
        Q_EMIT statusChanged();
        return;
    }
    if (!Secrets::available()) {
        m_claudeKeyError = i18n("No keyring is running to keep the key in.");
        Q_EMIT claudeKeyChanged();
        return;
    }
    Secrets::store(claudeKeyAttributes(), i18n("Athenaeum: Anthropic API key"), k, [this, k](bool ok, const QString &error) {
        if (ok)
            m_client->setClaudeKey(k);
        else
            m_claudeKeyError = error.isEmpty() ? i18n("The keyring did not keep the key.") : error;
        Q_EMIT claudeKeyChanged();
        Q_EMIT statusChanged();
    });
}

QString ModelSettings::provider() const
{
    return AppSettings::aiProvider();
}

void ModelSettings::setProvider(const QString &provider)
{
    if (provider == this->provider())
        return;
    AppSettings::setAiProvider(provider);
    m_swapping = false;
    Q_EMIT settingsChanged();
    Q_EMIT statusChanged();
}

bool ModelSettings::claude() const
{
    return provider() == QLatin1String("claude");
}

QString ModelSettings::claudeModel() const
{
    const QString m = AppSettings::claudeModel();
    return Claude::isClaudeModel(m) ? m : Claude::defaultModel();
}

void ModelSettings::setClaudeModel(const QString &model)
{
    if (!Claude::isClaudeModel(model.trimmed()) || model.trimmed() == claudeModel())
        return;
    AppSettings::setClaudeModel(model);
    Q_EMIT settingsChanged();
    Q_EMIT statusChanged();
}

QStringList ModelSettings::claudeModels() const
{
    return Claude::models();
}

QString ModelSettings::url() const
{
    return AppSettings::ollamaUrl();
}

void ModelSettings::setUrl(const QString &url)
{
    QString u = url.trimmed();
    if (!u.contains(QLatin1String("://")))
        u.prepend(QLatin1String("http://"));
    if (u == this->url())
        return;
    AppSettings::setValue(QStringLiteral("ollama.url"), u);
    m_client->setBaseUrl(u);
    Q_EMIT settingsChanged();
    refresh();
}

QString ModelSettings::answerModel() const
{
    return claude() ? claudeModel() : ollamaAnswerModel();
}

QString ModelSettings::ollamaAnswerModel() const
{
    return AppSettings::answerModel();
}

void ModelSettings::setOllamaAnswerModel(const QString &model)
{
    if (model.trimmed().isEmpty() || model.trimmed() == ollamaAnswerModel())
        return;
    AppSettings::setValue(QStringLiteral("models.answer"), model.trimmed());
    m_swapping = false;
    Q_EMIT settingsChanged();
    Q_EMIT statusChanged();
}

QString ModelSettings::embeddingModel() const
{
    return AppSettings::embeddingModel();
}

void ModelSettings::setEmbeddingModel(const QString &model)
{
    if (model.trimmed().isEmpty() || model.trimmed() == embeddingModel())
        return;
    AppSettings::setValue(QStringLiteral("models.embedding"), model.trimmed());
    m_swapping = false;
    Q_EMIT settingsChanged();
    Q_EMIT statusChanged();
}

QString ModelSettings::defaultAnswerModel() const
{
    return QStringLiteral("qwen3.5:2b");
}

QString ModelSettings::defaultEmbeddingModel() const
{
    return QStringLiteral("nomic-embed-text");
}

bool ModelSettings::has(const QString &model) const
{
    const QString want = Embedding::normalizeName(model);
    for (const QString &m : m_installed)
        if (Embedding::normalizeName(m) == want)
            return true;
    return false;
}

bool ModelSettings::answerModelInstalled() const
{
    if (claude())
        return claudeKeySet();
    return has(answerModel());
}

bool ModelSettings::embeddingModelInstalled() const
{
    return has(embeddingModel());
}

void ModelSettings::refresh()
{
    m_checking = true;
    const quint64 refresh = ++m_refresh;
    Q_EMIT statusChanged();
    m_client->installed(this, [this, refresh](const QStringList &names, const QString &error) {
        if (refresh != m_refresh)
            return;
        m_reachable = error.isEmpty();
        m_error = error;
        m_installed = names;
        m_installed.sort();
        m_client->loaded(this, [this, refresh](const QStringList &loaded, const QString &) {
            if (refresh != m_refresh)
                return;
            m_loaded = loaded;
            m_checking = false;
            m_checked = true;
            Q_EMIT statusChanged();
        });
    });
}

void ModelSettings::checkSwapping()
{
    m_client->loaded(this, [this](const QStringList &loaded, const QString &error) {
        if (!error.isEmpty())
            return;
        m_loaded = loaded;
        bool answer = false, embedding = false;
        for (const QString &m : loaded) {
            answer = answer || Embedding::normalizeName(m) == Embedding::normalizeName(answerModel());
            embedding = embedding || Embedding::normalizeName(m) == Embedding::normalizeName(embeddingModel());
        }
        // Right after an answer the embedding model has just checked the citations. If the answer model is gone, Ollama
        // made room for it: every question then loads both again (about 2 s on this laptop, measured)
        m_swapping = !claude() && embedding && !answer;
        Q_EMIT statusChanged();
    });
}
