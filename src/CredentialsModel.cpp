/******************************************************************************
 **  Copyright (c) Raoul Hecky. All Rights Reserved.
 **
 **  Moolticute is free software; you can redistribute it and/or modify
 **  it under the terms of the GNU General Public License as published by
 **  the Free Software Foundation; either version 3 of the License, or
 **  (at your option) any later version.
 **
 **  Moolticute is distributed in the hope that it will be useful,
 **  but WITHOUT ANY WARRANTY; without even the implied warranty of
 **  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 **  GNU General Public License for more details.
 **
 **  You should have received a copy of the GNU General Public License
 **  along with Foobar; if not, write to the Free Software
 **  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 **
 ******************************************************************************/
#include "CredentialsModel.h"

CredentialsFilterModel::CredentialsFilterModel(QObject *parent):
    QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
}

void CredentialsFilterModel::setFilter(const QString &filter_str)
{
    filter = filter_str.toLower();
    invalidate();
}

int CredentialsFilterModel::indexToSource(int idx)
{
    return mapToSource(index(idx, 0)).row();
}

int CredentialsFilterModel::indexFromSource(int idx)
{
    return mapFromSource(index(idx, 0)).row();
}

bool CredentialsFilterModel::filterAcceptsRow(int source_row, const QModelIndex &source_parent) const
{
    Q_UNUSED(source_parent)

    //shortcut
    if (filter.isEmpty())
        return true;

    CredentialsModel *credModel = qobject_cast<CredentialsModel *>(sourceModel());

    for(int idx : {CredentialsModel::ServiceIdx, CredentialsModel::LoginIdx, CredentialsModel::DescriptionIdx}) {
        if(credModel->data(credModel->index(source_row, idx)).toString().toLower().contains(filter.toLower()))
            return true;
    }

    return false;
}

bool CredentialsFilterModel::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    CredentialsModel *credModel = qobject_cast<CredentialsModel *>(sourceModel());

    if(left.row() < 0 || left.row() >= credModel->rowCount() || right.row() < 0 || right.row() >= credModel->rowCount())
        return false;

    const auto & l = credModel->at(left.row());
    const auto & r = credModel->at(right.row());

    return l.service < r.service || l.login < r.login;
}

CredentialsModel::CredentialsModel(QObject *parent):
    QAbstractTableModel(parent)
{
}

void CredentialsModel::load(const QJsonArray &json)
{
    QVector<Credential> creds;
    for (int i = 0;i < json.size();i++)
    {
        QJsonObject pnode = json.at(i).toObject();

        QJsonArray jchilds = pnode["childs"].toArray();
        for (int j = 0;j < jchilds.size();j++)
        {
            QJsonObject cnode = jchilds.at(j).toObject();

            Credential cred;
            cred.service = pnode["service"].toString();
            cred.login   = cnode["login"].toString();
            cred.description = cnode["description"].toString();
            cred.createdDate = QDate::fromString(cnode["date_created"].toString(), Qt::ISODate);
            cred.updatedDate = QDate::fromString(cnode["date_last_used"].toString(), Qt::ISODate);
            QJsonArray a = cnode["address"].toArray();
            if (a.size() < 2)
            {
                qWarning() << "Moolticute daemon did not send the node address, please upgrade moolticute daemon.";
                continue;
            }
            cred.address.append((char)a.at(0).toInt());
            cred.address.append((char)a.at(1).toInt());

            cred.favorite = cnode["favorite"].toInt();

            creds.append(std::move(cred));
        }
    }
    mergeWith(creds);

}



int CredentialsModel::rowCount(const QModelIndex &) const {
    return m_credentials.size();
}

int CredentialsModel::columnCount(const QModelIndex &) const {
    return ColumnCount;
}


QVariant CredentialsModel::data(const QModelIndex &index, int role) const {
    if(m_credentials.size() < index.row())
        return {};

    const Credential & cred = m_credentials.at(index.row());

    if(role == Qt::DisplayRole || role == Qt::EditRole) {
        switch(index.column()) {
        case ServiceIdx: return cred.service;
        case LoginIdx: return cred.login;
        case PasswordIdx: return cred.password;
        case DescriptionIdx: return cred.description;
        case DateCreatedIdx: if(cred.createdDate.isNull()) return tr("N/A"); return cred.createdDate;
        case DateModifiedIdx: if(cred.updatedDate.isNull()) return tr("N/A"); return cred.updatedDate;
        case FavoriteIdx: return cred.favorite;
        }

    }
    //Let us display the login & the service in the same column. Bit of a hack...
    if(role == LoginRole && index.column() == ServiceIdx) {
        return cred.login;
    }
    if(role == FavRole && index.column() == ServiceIdx) {
        return cred.favorite;
    }

    if(role == PasswordUnlockedRole && index.column() == PasswordIdx) {
        return !cred.password.isEmpty();
    }


    return {};
}

void CredentialsModel::setClearTextPassword(const QString & service, const QString & login, const QString & password) {
    auto it = std::find_if(std::begin(m_credentials), std::end(m_credentials), [&login, &service](const Credential & c) {
        return c.login == login && c.service == service;
    });
    if(it != std::end(m_credentials)) {
        const auto idx = it - std::begin(m_credentials);
        it->password = password;
        Q_EMIT dataChanged(index(idx, PasswordIdx),index(idx, PasswordIdx));
    }
}


void CredentialsModel::update(const QString & service, const QString & login, const QString & description) {
    auto it = std::find_if(std::begin(m_credentials), std::end(m_credentials), [&login, &service](const Credential & c) {
        return c.login == login && c.service == service;
    });
    if(it != std::end(m_credentials)) {
        const auto idx = it - std::begin(m_credentials);
        it->description = description;
        it->password.clear();
        Q_EMIT dataChanged(index(idx, PasswordIdx),index(idx, DescriptionIdx));
    }
    else {
        Credential c;
        c.service = service;
        c.login = login;
        c.description = description;
        beginInsertRows(QModelIndex(), m_credentials.size(), m_credentials.size());
        m_credentials << c;
        endInsertRows();
    }
}


void  CredentialsModel::mergeWith(const QVector<Credential> & newCreds) {

    for(const Credential & newCred: newCreds) {
        auto it = std::find_if(std::begin(m_credentials), std::end(m_credentials), [&newCred](const Credential & c) {
            return c.login == newCred.login && c.service == newCred.service;
        });
        if(it == std::end(m_credentials)) {
            beginInsertRows(QModelIndex(), m_credentials.size(), m_credentials.size());
            m_credentials << newCred;
            endInsertRows();
            continue;
        }
        if(it->description != newCred.description || it->createdDate != newCred.createdDate || it->updatedDate != newCred.updatedDate){
            *it = newCred;
            const auto idx = it - std::begin(m_credentials);
            dataChanged(index(idx, LoginIdx +1), index(idx, ColumnCount-1));
        }
    }
    if(newCreds.size() != m_credentials.size()) {
        QVector<Credential>::size_type idx = 0;
        while(idx < m_credentials.size())  {
            const Credential & oldCred = m_credentials.at(idx);
            auto it = std::find_if(std::begin(newCreds), std::end(newCreds), [&oldCred](const Credential & c) {
                return c.login == oldCred.login && c.service == oldCred.service;
            });
            if(it == std::end(newCreds)) {
                beginRemoveRows({}, idx, idx);
                m_credentials.remove(idx);
                endRemoveRows();
            }
            else  {
                idx++;
            }
        }
    }
}

auto CredentialsModel::at(int idx) const  -> const Credential & {
    return m_credentials.at(idx);
}

bool CredentialsModel::credExists(QString service, QString login)
{
    for (const Credential &cred: qAsConst(m_credentials))
    {
        if (cred.service == service &&
            cred.login == login)
            return true;
    }
    return false;
}

void CredentialsModel::credAdd(QString service, QString login, QString password)
{
    Credential newCred;
    newCred.service = service;
    newCred.login = login;
    newCred.password = password;

    //When this is a completely new cred, just add it
    if (!deletedCredentials.contains(newCred) &&
        !m_credentials.contains(newCred))
    {
        addedCredentials.append(newCred);

        beginInsertRows(QModelIndex(), m_credentials.size(), m_credentials.size());
        m_credentials << c;
        endInsertRows();

        return;
    }

    //cred exists already in list, update it
    if (m_credentials.contains(newCred))
    {
        auto it = std::find(std::begin(m_credentials), std::end(m_credentials), newCred);

        //update listview
        if (it != std::end(m_credentials))
        {
            const auto idx = it - std::begin(m_credentials);
            it->password.clear();
            Q_EMIT dataChanged(index(idx, PasswordIdx),index(idx, DescriptionIdx));
        }

        if (changedCredentials.contains(newCred))
        {
            auto it = std::find(std::begin(changedCredentials), std::end(changedCredentials), newCred);
            if (it != std::end(changedCredentials))
            {
                it->login = newCred.login;
                it->password = newCred.password;
            }
        }

        if (addedCredentials.contains(newCred))
        {
            auto it = std::find(std::begin(addedCredentials), std::end(addedCredentials), newCred);
            if (it != std::end(addedCredentials))
            {
                it->login = newCred.login;
                it->password = newCred.password;
            }
        }

        return;
    }

    if (deletedCredentials.contains(newCred))
    {
        //If it were deleted, remove it from the deleted list
        //and add it back to the current list
        auto it = std::find(std::begin(origCredentials), std::end(origCredentials), newCred);
        if (it != std::end(origCredentials))
        {
            //The cred was already added before, add it back
            beginInsertRows(QModelIndex(), m_credentials.size(), m_credentials.size());
            m_credentials << c;
            endInsertRows();
        }

        deletedCredentials.removeAll(newCred);
    }
}

void CredentialsModel::credUpdate(QString service, QString login, QString password, QString desc, int fav)
{

}

void CredentialsModel::credDelete(QString service, QString login)
{

}
