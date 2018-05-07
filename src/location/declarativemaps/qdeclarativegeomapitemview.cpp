/****************************************************************************
**
** Copyright (C) 2015 Jolla Ltd.
** Contact: Aaron McCarthy <aaron.mccarthy@jollamobile.com>
** Copyright (C) 2015 The Qt Company Ltd.
** Contact: http://www.qt.io/licensing/
**
** This file is part of the QtLocation module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL3$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see http://www.qt.io/terms-conditions. For further
** information use the contact form at http://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPLv3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or later as published by the Free
** Software Foundation and appearing in the file LICENSE.GPL included in
** the packaging of this file. Please review the following information to
** ensure the GNU General Public License version 2.0 requirements will be
** met: http://www.gnu.org/licenses/gpl-2.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qdeclarativegeomapitemview_p.h"
#include "qdeclarativegeomap_p.h"
#include "qdeclarativegeomapitembase_p.h"
#include "mapitemviewdelegateincubator_p.h"

#include <QtCore/QAbstractItemModel>
#include <QtQml/QQmlContext>
#include <QtQml/private/qqmldelegatemodel_p.h>
#include <QtQml/private/qqmlopenmetaobject_p.h>
#include <QtQuick/private/qquickanimation_p.h>
#include <QtQml/QQmlListProperty>

QT_BEGIN_NAMESPACE

/*!
    \qmltype MapItemView
    \instantiates QDeclarativeGeoMapItemView
    \inqmlmodule QtLocation
    \ingroup qml-QtLocation5-maps
    \since QtLocation 5.5
    \inherits QObject

    \brief The MapItemView is used to populate Map from a model.

    The MapItemView is used to populate Map with MapItems from a model.
    The MapItemView type only makes sense when contained in a Map,
    meaning that it has no standalone presentation.

    \section2 Example Usage

    This example demonstrates how to use the MapViewItem object to display
    a \l{Route}{route} on a \l{Map}{map}:

    \snippet declarative/maps.qml QtQuick import
    \snippet declarative/maps.qml QtLocation import
    \codeline
    \snippet declarative/maps.qml MapRoute
*/

/*!
    \qmlproperty Transition QtLocation::MapItemView::add

    This property holds the transition that is applied to the map items created by the view
    when they are instantiated and added to the map.

    \since QtLocation 5.12
*/

/*!
    \qmlproperty Transition QtLocation::MapItemView::remove

    This property holds the transition that is applied to the map items created by the view
    when they are removed.

    \since QtLocation 5.12
*/

QDeclarativeGeoMapItemView::QDeclarativeGeoMapItemView(QQuickItem *parent)
    : QDeclarativeGeoMapItemGroup(parent), m_componentCompleted(false), m_delegate(0),
      m_map(0), m_fitViewport(false), m_delegateModel(0)
{
        m_exit = new QQuickTransition(this);
        QQmlListProperty<QQuickAbstractAnimation> anims = m_exit->animations();
        QQuickNumberAnimation *ani = new QQuickNumberAnimation(m_exit);
        ani->setProperty(QStringLiteral("opacity"));
        ani->setTo(0.0);
        ani->setDuration(300.0);
        anims.append(&anims, ani);
}

QDeclarativeGeoMapItemView::~QDeclarativeGeoMapItemView()
{
    removeInstantiatedItems();
}

/*!
    \internal
*/
void QDeclarativeGeoMapItemView::componentComplete()
{
    QDeclarativeGeoMapItemGroup::componentComplete();
    m_componentCompleted = true;
    if (!m_itemModel.isNull())
        m_delegateModel->setModel(m_itemModel);

    if (m_delegate)
        m_delegateModel->setDelegate(m_delegate);

    m_delegateModel->componentComplete();
}

void QDeclarativeGeoMapItemView::classBegin()
{
    QDeclarativeGeoMapItemGroup::classBegin();
    QQmlContext *ctx = qmlContext(this);
    m_delegateModel = new QQmlDelegateModel(ctx, this);
    m_delegateModel->classBegin();

    connect(m_delegateModel, &QQmlInstanceModel::modelUpdated, this, &QDeclarativeGeoMapItemView::modelUpdated);
    connect(m_delegateModel, &QQmlInstanceModel::createdItem, this, &QDeclarativeGeoMapItemView::createdItem);
    connect(m_delegateModel, &QQmlInstanceModel::destroyingItem, this, &QDeclarativeGeoMapItemView::destroyingItem);
    connect(m_delegateModel, &QQmlInstanceModel::initItem, this, &QDeclarativeGeoMapItemView::initItem);
}

void QDeclarativeGeoMapItemView::destroyingItem(QObject */*object*/)
{

}

void QDeclarativeGeoMapItemView::initItem(int /*index*/, QObject */*object*/)
{

}

void QDeclarativeGeoMapItemView::createdItem(int index, QObject */*object*/)
{
    if (!m_map)
        return;
    // createdItem is emitted on asynchronous creation. In which case, object has to be invoked again.
    // See QQmlDelegateModel::object for further info.

    if (m_incubationMode == QQmlIncubator::Synchronous) {
        qWarning() << "createdItem invoked on Synchronous incubation";
        return;
    }

    QQuickItem *item = qobject_cast<QQuickItem *>(m_delegateModel->object(index, m_incubationMode));
    if (item)
        addDelegateToMap(item, index);
    else
        qWarning() << "createdItem for " << index << " produced a null item";
}

void QDeclarativeGeoMapItemView::modelUpdated(const QQmlChangeSet &changeSet, bool reset)
{
    // move changes are expressed as one remove + one insert, with the same moveId.
    // For simplicity, they will be treated as remove + insert.
    // Changes will be also ignored, as they represent only data changes, not layout changes
    if (reset) { // Assuming this means "remove everything already instantiated"
        removeInstantiatedItems();
    } else {
        // Remove items from the back to the front to retain the mapping to what is received from the changesets
        const QVector<QQmlChangeSet::Change> &removes = changeSet.removes();
        std::map<int, int> mapRemoves;
        for (int i = 0; i < removes.size(); i++)
            mapRemoves.insert(std::pair<int, int>(removes.at(i).start(), i));

        for (auto rit = mapRemoves.rbegin(); rit != mapRemoves.rend(); ++rit) {
            const QQmlChangeSet::Change &c = removes.at(rit->second);
            for (int idx = c.end() - 1; idx >= c.start(); --idx)
                removeDelegateFromMap(idx);
        }
    }

    for (const QQmlChangeSet::Change &c: changeSet.inserts()) {
        for (int idx = c.start(); idx < c.end(); idx++)
            addDelegateToMap(qobject_cast<QQuickItem *>(m_delegateModel->object(idx, m_incubationMode)), idx);
    }

    fitViewport();
}

/*!
    \qmlproperty model QtLocation::MapItemView::model

    This property holds the model that provides data used for creating the map items defined by the
    delegate. Only QAbstractItemModel based models are supported.
*/
QVariant QDeclarativeGeoMapItemView::model() const
{
    return m_itemModel;
}

void QDeclarativeGeoMapItemView::setModel(const QVariant &model)
{
    if (model == m_itemModel)
        return;

    m_itemModel = model;
    if (m_componentCompleted)
        m_delegateModel->setModel(m_itemModel);

    emit modelChanged();
}

/*!
    \qmlproperty Component QtLocation::MapItemView::delegate

    This property holds the delegate which defines how each item in the
    model should be displayed. The Component must contain exactly one
    MapItem -derived object as the root object.
*/
QQmlComponent *QDeclarativeGeoMapItemView::delegate() const
{
    return m_delegate;
}

void QDeclarativeGeoMapItemView::setDelegate(QQmlComponent *delegate)
{
    if (m_delegate == delegate)
        return;

    m_delegate = delegate;
    if (m_componentCompleted)
        m_delegateModel->setDelegate(m_delegate);

    emit delegateChanged();
}

/*!
    \qmlproperty Component QtLocation::MapItemView::autoFitViewport

    This property controls whether to automatically pan and zoom the viewport
    to display all map items when items are added or removed.

    Defaults to false.
*/
bool QDeclarativeGeoMapItemView::autoFitViewport() const
{
    return m_fitViewport;
}

void QDeclarativeGeoMapItemView::setAutoFitViewport(const bool &fit)
{
    if (fit == m_fitViewport)
        return;
    m_fitViewport = fit;
    fitViewport();
    emit autoFitViewportChanged();
}

/*!
    \internal
*/
void QDeclarativeGeoMapItemView::fitViewport()
{

    if (!m_map || !m_map->mapReady() || !m_fitViewport)
        return;

    if (m_map->mapItems().size() > 0)
        m_map->fitViewportToMapItems();
}

/*!
    \internal
*/
void QDeclarativeGeoMapItemView::setMap(QDeclarativeGeoMap *map)
{
    if (!map || m_map) // changing map on the fly not supported
        return;
    m_map = map;
    instantiateAllItems();
}

/*!
    \internal
*/
void QDeclarativeGeoMapItemView::removeInstantiatedItems(bool transition)
{
    if (!m_map)
        return;

    // FIXME: removeInstantiatedItems should abort, as well as exit transitions terminated QTBUG-69195
    // Backward as removeItemFromMap modifies m_instantiatedItems
    for (int i = m_instantiatedItems.size() -1; i >= 0 ; i--)
        removeDelegateFromMap(i, transition);
}

/*!
    \internal

    Instantiates all items.
*/
void QDeclarativeGeoMapItemView::instantiateAllItems()
{
    // The assumption is that if m_instantiatedItems isn't empty, instantiated items have been already added
    if (!m_componentCompleted || !m_map || !m_delegate || m_itemModel.isNull() || !m_instantiatedItems.isEmpty())
        return;

    // If here, m_delegateModel may contain data, but QQmlInstanceModel::object for each row hasn't been called yet.
    for (int i = 0; i < m_delegateModel->count(); i++)
        addDelegateToMap(qobject_cast<QQuickItem *>(m_delegateModel->object(i, m_incubationMode)), i);

    fitViewport();
}

void QDeclarativeGeoMapItemView::setIncubateDelegates(bool useIncubators)
{
    const QQmlIncubator::IncubationMode incubationMode =
            (useIncubators) ? QQmlIncubator::Asynchronous : QQmlIncubator::Synchronous;
    if (m_incubationMode == incubationMode)
        return;
    m_incubationMode = incubationMode;
    emit incubateDelegatesChanged();
}

bool QDeclarativeGeoMapItemView::incubateDelegates() const
{
    return m_incubationMode == QQmlIncubator::Asynchronous;
}

QList<QQuickItem *> QDeclarativeGeoMapItemView::mapItems()
{
    return m_instantiatedItems;
}

void QDeclarativeGeoMapItemView::removeDelegateFromMap(int index, bool transition)
{
    if (index >= 0 && index < m_instantiatedItems.size()) {
        QQuickItem *item = m_instantiatedItems.takeAt(index);
        if (!item) {
            if (m_incubatingItems.contains(index)) {
                // cancel request
                m_delegateModel->cancel(index);
                m_incubatingItems.remove(index);
            }
            return;
        }
        // item can be either a QDeclarativeGeoMapItemBase or a QDeclarativeGeoMapItemGroup
        if (m_exit && m_map &&  transition) {
            transitionItemOut(item);
        } else {
            disconnect(item, 0, this, 0);
            removeDelegateFromMap(item);
            item->setParentItem(nullptr);   // Needed because
            item->setParent(nullptr);       // m_delegateModel->release(item) does not destroy the item most of the times!!
            QQmlInstanceModel::ReleaseFlags releaseStatus = m_delegateModel->release(item);
#ifdef QT_DEBUG
            if (releaseStatus == QQmlInstanceModel::Referenced)
                qWarning() << "item "<< index << "(" << item << ") still referenced";
#else
            Q_UNUSED(releaseStatus)
#endif
        }
    }
}

void QDeclarativeGeoMapItemView::removeDelegateFromMap(QQuickItem *o)
{
    if (!m_map)
        return;

    QDeclarativeGeoMapItemBase *item = qobject_cast<QDeclarativeGeoMapItemBase *>(o);
    if (item) {
        m_map->removeMapItem(item);
        return;
    }
    QDeclarativeGeoMapItemView *view = qobject_cast<QDeclarativeGeoMapItemView *>(o);
    if (view) {
        m_map->removeMapItemView(view);
        return;
    }
    QDeclarativeGeoMapItemGroup *group = qobject_cast<QDeclarativeGeoMapItemGroup *>(o);
    if (group) {
        m_map->removeMapItemGroup(group);
        return;
    }
}

void QDeclarativeGeoMapItemView::transitionItemOut(QQuickItem *o)
{
    QDeclarativeGeoMapItemGroup *group = qobject_cast<QDeclarativeGeoMapItemGroup *>(o);
    if (group) {
        if (!group->m_transitionManager) {
            QScopedPointer<QDeclarativeGeoMapItemTransitionManager>manager(new QDeclarativeGeoMapItemTransitionManager(group));
            group->m_transitionManager.swap(manager);
            group->m_transitionManager->m_view = this;
        }
        connect(group, SIGNAL(removeTransitionFinished()),
                this, SLOT(exitTransitionFinished()));

        group->m_transitionManager->transitionExit();
        return;
    }
    QDeclarativeGeoMapItemBase *item = qobject_cast<QDeclarativeGeoMapItemBase *>(o);
    if (item) {
        if (!item->m_transitionManager) {
            QScopedPointer<QDeclarativeGeoMapItemTransitionManager> manager(new QDeclarativeGeoMapItemTransitionManager(item));
            item->m_transitionManager.swap(manager);
            item->m_transitionManager->m_view = this;
        }
        connect(item, SIGNAL(removeTransitionFinished()),
                this, SLOT(exitTransitionFinished()) );

        item->m_transitionManager->transitionExit();
        return;
    }
}

void QDeclarativeGeoMapItemView::exitTransitionFinished()
{
    QQuickItem *item = qobject_cast<QQuickItem *>(sender());
    if (!item)
        return;
    disconnect(item, 0, this, 0);
    removeDelegateFromMap(item);
    item->setParentItem(nullptr);
    item->setParent(nullptr);
    QQmlInstanceModel::ReleaseFlags releaseStatus = m_delegateModel->release(item);
#ifdef QT_DEBUG
    if (releaseStatus == QQmlInstanceModel::Referenced)
        qWarning() << "item "<<item<<" still referenced";
#else
    Q_UNUSED(releaseStatus)
#endif
}

void QDeclarativeGeoMapItemView::addItemToMap(QDeclarativeGeoMapItemBase *item, int index)
{

    if (m_map && item) { // belonging to another map??
        if (item->quickMap() == m_map)
            return;
    }

    if (m_map) {
        insertInstantiatedItem(index, item);
        item->setParentItem(this);
        m_map->addMapItem(item);
        if (m_enter) {
            if (!item->m_transitionManager) {
                QScopedPointer<QDeclarativeGeoMapItemTransitionManager>manager(new QDeclarativeGeoMapItemTransitionManager(item));
                item->m_transitionManager.swap(manager);
            }
            item->m_transitionManager->m_view = this;
            item->m_transitionManager->transitionEnter();
        }
    }
}

void QDeclarativeGeoMapItemView::insertInstantiatedItem(int index, QQuickItem *o)
{
    if (m_incubatingItems.contains(index)) {
        m_incubatingItems.remove(index);
        m_instantiatedItems.replace(index, o);
    } else {
        m_instantiatedItems.insert(index, o);
    }
}

void QDeclarativeGeoMapItemView::addItemViewToMap(QDeclarativeGeoMapItemView *item, int index)
{
    if (!item || (m_map && item->quickMap() == m_map))
        return;

    if (m_map) {
        insertInstantiatedItem(index, item);
        item->setParentItem(this);
        m_map->addMapItemView(item);
        if (m_enter) {
            if (!item->m_transitionManager) {
                QScopedPointer<QDeclarativeGeoMapItemTransitionManager> manager(new QDeclarativeGeoMapItemTransitionManager(item));
                item->m_transitionManager.swap(manager);
            }
            item->m_transitionManager->m_view = this;
            item->m_transitionManager->transitionEnter();
        }
    }
}

void QDeclarativeGeoMapItemView::addItemGroupToMap(QDeclarativeGeoMapItemGroup *item, int index)
{
    if (!item || (m_map && item->quickMap() == m_map))
        return;

    if (m_map) {
        insertInstantiatedItem(index, item);
        item->setParentItem(this);
        m_map->addMapItemGroup(item);
        if (m_enter) {
            if (!item->m_transitionManager) {
                QScopedPointer<QDeclarativeGeoMapItemTransitionManager>manager(new QDeclarativeGeoMapItemTransitionManager(item));
                item->m_transitionManager.swap(manager);
            }
            item->m_transitionManager->m_view = this;
            item->m_transitionManager->transitionEnter();
        }
    }
}

void QDeclarativeGeoMapItemView::addDelegateToMap(QQuickItem *object, int index)
{
    if (!object) {
        m_incubatingItems.insert(index);
        m_instantiatedItems.insert(index, nullptr);
        return;
    }
    QDeclarativeGeoMapItemBase *item = qobject_cast<QDeclarativeGeoMapItemBase *>(object);
    if (item) { // else createdItem will be emitted.
        addItemToMap(item, index);
        return;
    }
    QDeclarativeGeoMapItemView *view = qobject_cast<QDeclarativeGeoMapItemView *>(object);
    if (view) {
        addItemViewToMap(view, index);
        return;
    }
    QDeclarativeGeoMapItemGroup *group = qobject_cast<QDeclarativeGeoMapItemGroup *>(object);
    if (group) {
        addItemGroupToMap(group, index);
        return;
    }
}

QT_END_NAMESPACE


