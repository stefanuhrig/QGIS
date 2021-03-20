/***************************************************************************
   qgshanasourceselect.cpp
   --------------------------------------
   Date      : 31-05-2019
   Copyright : (C) SAP SE
   Author    : Maxim Rylov
 ***************************************************************************/

/***************************************************************************
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 ***************************************************************************/
#include "qgsapplication.h"
#include "qgsdatasourceuri.h"
#include "qgsgui.h"
#include "qgshanasourceselect.h"
#include "qgshanaconnection.h"
#include "qgshananewconnection.h"
#include "qgshanaprovider.h"
#include "qgshanatablemodel.h"
#include "qgshanautils.h"
#include "qgslogger.h"
#include "qgsmanageconnectionsdialog.h"
#include "qgsproxyprogresstask.h"
#include "qgsquerybuilder.h"
#include "qgsvectorlayer.h"
#include "qgssettings.h"

#include <QComboBox>
#include <QFileDialog>
#include <QHeaderView>
#include <QInputDialog>
#include <QMessageBox>
#include <QStringList>
#include <QStyledItemDelegate>

//! Used to create an editor for when the user tries to change the contents of a cell
QWidget *QgsHanaSourceSelectDelegate::createEditor(
  QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index ) const
{
  Q_UNUSED( option );

  QString tableName = index.sibling( index.row(), QgsHanaTableModel::DbtmTable ).data( Qt::DisplayRole ).toString();
  if ( tableName.isEmpty() )
    return nullptr;

  if ( index.column() == QgsHanaTableModel::DbtmSql )
  {
    return new QLineEdit( parent );
  }

  if ( index.column() == QgsHanaTableModel::DbtmGeomType && index.data( Qt::UserRole + 1 ).toBool() )
  {
    QComboBox *cb = new QComboBox( parent );
    for ( QgsWkbTypes::Type type :
          QList<QgsWkbTypes::Type>()
          << QgsWkbTypes::Point
          << QgsWkbTypes::LineString
          << QgsWkbTypes::Polygon
          << QgsWkbTypes::MultiPoint
          << QgsWkbTypes::MultiLineString
          << QgsWkbTypes::MultiPolygon
          << QgsWkbTypes::CircularString
          << QgsWkbTypes::GeometryCollection
          << QgsWkbTypes::NoGeometry )
    {
      cb->addItem( QgsHanaTableModel::iconForWkbType( type ), QgsWkbTypes::displayString( type ), type );
    }
    return cb;
  }

  if ( index.column() == QgsHanaTableModel::DbtmPkCol )
  {
    const QStringList values = index.data( Qt::UserRole + 1 ).toStringList();

    if ( !values.isEmpty() )
    {
      QComboBox *cb = new QComboBox( parent );
      cb->setItemDelegate( new QStyledItemDelegate( parent ) );

      QStandardItemModel *model = new QStandardItemModel( values.size(), 1, cb );

      int row = 0;
      for ( const QString &value : values )
      {
        QStandardItem *item = new QStandardItem( value );
        item->setFlags( Qt::ItemIsUserCheckable | Qt::ItemIsEnabled );
        item->setCheckable( true );
        item->setData( Qt::Unchecked, Qt::CheckStateRole );
        model->setItem( row++, 0, item );
      }

      cb->setModel( model );

      return cb;
    }
  }

  if ( index.column() == QgsHanaTableModel::DbtmSrid )
  {
    QLineEdit *le = new QLineEdit( parent );
    le->setValidator( new QIntValidator( -1, 999999, parent ) );
    return le;
  }

  return nullptr;
}

void QgsHanaSourceSelectDelegate::setModelData(
  QWidget *editor, QAbstractItemModel *model, const QModelIndex &index ) const
{
  QComboBox *cb = qobject_cast<QComboBox *>( editor );
  if ( cb )
  {
    if ( index.column() == QgsHanaTableModel::DbtmGeomType )
    {
      QgsWkbTypes::Type type = static_cast<QgsWkbTypes::Type>( cb->currentData().toInt() );

      model->setData( index, QgsHanaTableModel::iconForWkbType( type ), Qt::DecorationRole );
      model->setData( index, type != QgsWkbTypes::Unknown ? QgsWkbTypes::displayString( type ) : tr( "Select…" ) );
      model->setData( index, type, Qt::UserRole + 2 );
    }
    else if ( index.column() == QgsHanaTableModel::DbtmPkCol )
    {
      QStandardItemModel *cbm = qobject_cast<QStandardItemModel *>( cb->model() );
      QStringList cols;
      for ( int idx = 0; idx < cbm->rowCount(); idx++ )
      {
        QStandardItem *item = cbm->item( idx, 0 );
        if ( item->data( Qt::CheckStateRole ) == Qt::Checked )
          cols << item->text();
      }

      model->setData( index, cols.isEmpty() ? tr( "Select…" ) : cols.join( QLatin1String( ", " ) ) );
      model->setData( index, cols, Qt::UserRole + 2 );
    }
  }

  QLineEdit *le = qobject_cast<QLineEdit *>( editor );
  if ( le )
  {
    QString value( le->text() );

    if ( index.column() == QgsHanaTableModel::DbtmSrid && value.isEmpty() )
      value = tr( "Enter…" );

    model->setData( index, value );
  }
}

void QgsHanaSourceSelectDelegate::setEditorData( QWidget *editor, const QModelIndex &index ) const
{
  QString value( index.data( Qt::DisplayRole ).toString() );

  QComboBox *cb = qobject_cast<QComboBox *>( editor );
  if ( cb )
  {
    if ( index.column() == QgsHanaTableModel::DbtmGeomType )
      cb->setCurrentIndex( cb->findData( index.data( Qt::UserRole + 2 ).toInt() ) );

    if ( index.column() == QgsHanaTableModel::DbtmPkCol &&
         !index.data( Qt::UserRole + 2 ).toStringList().isEmpty() )
    {
      const QStringList columns = index.data( Qt::UserRole + 2 ).toStringList();
      for ( const QString &colName : columns )
      {
        QStandardItemModel *cbm = qobject_cast<QStandardItemModel *>( cb->model() );
        for ( int idx = 0; idx < cbm->rowCount(); ++idx )
        {
          QStandardItem *item = cbm->item( idx, 0 );
          if ( item->text() != colName )
            continue;

          item->setData( Qt::Checked, Qt::CheckStateRole );
          break;
        }
      }
    }
  }

  QLineEdit *le = qobject_cast<QLineEdit *>( editor );
  if ( le )
  {
    bool ok;
    ( void )value.toInt( &ok );
    if ( index.column() == QgsHanaTableModel::DbtmSrid && !ok )
      value.clear();

    le->setText( value );
  }
}

QgsHanaSourceSelect::QgsHanaSourceSelect(
  QWidget *parent,
  Qt::WindowFlags fl,
  QgsProviderRegistry::WidgetMode theWidgetMode )
  : QgsAbstractDataSourceWidget( parent, fl, theWidgetMode )
{
  setupUi( this );
  QgsGui::instance()->enableAutoGeometryRestore( this );

  connect( btnConnect, &QPushButton::clicked, this, &QgsHanaSourceSelect::btnConnect_clicked );
  connect( cbxAllowGeometrylessTables, &QCheckBox::stateChanged, this, &QgsHanaSourceSelect::cbxAllowGeometrylessTables_stateChanged );
  connect( btnNew, &QPushButton::clicked, this, &QgsHanaSourceSelect::btnNew_clicked );
  connect( btnEdit, &QPushButton::clicked, this, &QgsHanaSourceSelect::btnEdit_clicked );
  connect( btnDelete, &QPushButton::clicked, this, &QgsHanaSourceSelect::btnDelete_clicked );
  connect( btnSave, &QPushButton::clicked, this, &QgsHanaSourceSelect::btnSave_clicked );
  connect( btnLoad, &QPushButton::clicked, this, &QgsHanaSourceSelect::btnLoad_clicked );
  connect( mSearchGroupBox, &QGroupBox::toggled, this, &QgsHanaSourceSelect::mSearchGroupBox_toggled );
  connect( mSearchTableEdit, &QLineEdit::textChanged, this, &QgsHanaSourceSelect::mSearchTableEdit_textChanged );
  connect( mSearchColumnComboBox, &QComboBox::currentTextChanged, this, &QgsHanaSourceSelect::mSearchColumnComboBox_currentTextChanged );
  connect( mSearchModeComboBox, &QComboBox::currentTextChanged, this, &QgsHanaSourceSelect::mSearchModeComboBox_currentTextChanged );
  connect( cmbConnections, static_cast<void ( QComboBox::* )( int )>( &QComboBox::activated ),
           this, &QgsHanaSourceSelect::cmbConnections_activated );
  connect( mTablesTreeView, &QTreeView::clicked, this, &QgsHanaSourceSelect::mTablesTreeView_clicked );
  connect( mTablesTreeView, &QTreeView::doubleClicked, this, &QgsHanaSourceSelect::mTablesTreeView_doubleClicked );
  setupButtons( buttonBox );
  connect( buttonBox, &QDialogButtonBox::helpRequested, this, &QgsHanaSourceSelect::showHelp );

  if ( widgetMode() != QgsProviderRegistry::WidgetMode::None )
    mHoldDialogOpen->hide();
  else
    setWindowTitle( tr( "Add SAP HANA Table(s)" ) );

  mBuildQueryButton = new QPushButton( tr( "&Set Filter" ) );
  mBuildQueryButton->setToolTip( tr( "Set Filter" ) );
  mBuildQueryButton->setDisabled( true );

  if ( widgetMode() != QgsProviderRegistry::WidgetMode::Manager )
  {
    buttonBox->addButton( mBuildQueryButton, QDialogButtonBox::ActionRole );
    connect( mBuildQueryButton, &QAbstractButton::clicked, this, &QgsHanaSourceSelect::buildQuery );
  }

  populateConnectionList();

  mSearchModeComboBox->addItem( tr( "Wildcard" ) );
  mSearchModeComboBox->addItem( tr( "RegExp" ) );

  mSearchColumnComboBox->addItem( tr( "All" ) );
  mSearchColumnComboBox->addItem( tr( "Schema" ) );
  mSearchColumnComboBox->addItem( tr( "Table" ) );
  mSearchColumnComboBox->addItem( tr( "Comment" ) );
  mSearchColumnComboBox->addItem( tr( "Geometry column" ) );
  mSearchColumnComboBox->addItem( tr( "Type" ) );
  mSearchColumnComboBox->addItem( tr( "Feature id" ) );
  mSearchColumnComboBox->addItem( tr( "SRID" ) );
  mSearchColumnComboBox->addItem( tr( "Sql" ) );

  mProxyModel.setParent( this );
  mProxyModel.setFilterKeyColumn( -1 );
  mProxyModel.setFilterCaseSensitivity( Qt::CaseInsensitive );
  mProxyModel.setSourceModel( &mTableModel );

  mTablesTreeView->setModel( &mProxyModel );
  mTablesTreeView->setSortingEnabled( true );
  mTablesTreeView->setEditTriggers( QAbstractItemView::CurrentChanged );
  mTablesTreeView->setItemDelegate( new QgsHanaSourceSelectDelegate( this ) );

  connect( mTablesTreeView->selectionModel(), &QItemSelectionModel::selectionChanged,
           this, &QgsHanaSourceSelect::treeWidgetSelectionChanged );

  QgsSettings settings;
  mTablesTreeView->setSelectionMode( settings.value( QStringLiteral( "qgis/addHanaDC" ), false ).toBool() ?
                                     QAbstractItemView::ExtendedSelection : QAbstractItemView::MultiSelection );

  //for Qt < 4.3.2, passing -1 to include all model columns
  //in search does not seem to work
  mSearchColumnComboBox->setCurrentIndex( 2 );

  restoreGeometry( settings.value( QStringLiteral( "Windows/HanaSourceSelect/geometry" ) ).toByteArray() );
  mHoldDialogOpen->setChecked( settings.value( QStringLiteral( "Windows/HanaSourceSelect/HoldDialogOpen" ), false ).toBool() );

  for ( int i = 0; i < mTableModel.columnCount(); i++ )
  {
    mTablesTreeView->setColumnWidth( i, settings.value( QStringLiteral( "Windows/HanaSourceSelect/columnWidths/%1" )
                                     .arg( i ), mTablesTreeView->columnWidth( i ) ).toInt() );
  }

  //hide the search options by default
  //they will be shown when the user ticks
  //the search options group box
  mSearchLabel->setVisible( false );
  mSearchColumnComboBox->setVisible( false );
  mSearchColumnsLabel->setVisible( false );
  mSearchModeComboBox->setVisible( false );
  mSearchModeLabel->setVisible( false );
  mSearchTableEdit->setVisible( false );

  cbxAllowGeometrylessTables->setDisabled( true );
}

//! Autoconnected SLOTS *
// Slot for adding a new connection
void QgsHanaSourceSelect::btnNew_clicked()
{
  QgsHanaNewConnection nc( this );
  if ( nc.exec() )
  {
    populateConnectionList();
    emit connectionsChanged();
  }
}
// Slot for deleting an existing connection
void QgsHanaSourceSelect::btnDelete_clicked()
{
  QString msg = tr( "Are you sure you want to remove the %1 connection and all associated settings?" )
                .arg( cmbConnections->currentText() );
  if ( QMessageBox::Yes != QMessageBox::question( this, tr( "Confirm Delete" ), msg, QMessageBox::Yes | QMessageBox::No ) )
    return;

  QgsHanaSourceSelect::deleteConnection( cmbConnections->currentText() );

  populateConnectionList();
  emit connectionsChanged();
}

void QgsHanaSourceSelect::deleteConnection( const QString &name )
{
  QgsHanaSettings::removeConnection( name );
}

void QgsHanaSourceSelect::btnSave_clicked()
{
  QgsManageConnectionsDialog dlg( this, QgsManageConnectionsDialog::Export, QgsManageConnectionsDialog::HANA );
  dlg.exec();
}

void QgsHanaSourceSelect::btnLoad_clicked()
{
  QString fileName = QFileDialog::getOpenFileName( this, tr( "Load Connections" ),
                     QDir::homePath(), tr( "XML files (*.xml *XML)" ) );
  if ( fileName.isEmpty() )
  {
    return;
  }

  QgsManageConnectionsDialog dlg( this, QgsManageConnectionsDialog::Import,
                                  QgsManageConnectionsDialog::HANA, fileName );
  dlg.exec();
  populateConnectionList();
}

// Slot for editing a connection
void QgsHanaSourceSelect::btnEdit_clicked()
{
  QgsHanaNewConnection nc( this, cmbConnections->currentText() );
  if ( nc.exec() )
  {
    populateConnectionList();
    emit connectionsChanged();
  }
}

//! End Autoconnected SLOTS *

// Remember which database is selected
void QgsHanaSourceSelect::cmbConnections_activated( int )
{
  // Remember which database was selected.
  QgsHanaSettings::setSelectedConnection( cmbConnections->currentText() );

  cbxAllowGeometrylessTables->blockSignals( true );
  QgsHanaSettings settings( cmbConnections->currentText() );
  settings.load();
  cbxAllowGeometrylessTables->setChecked( settings.allowGeometrylessTables() );
  cbxAllowGeometrylessTables->blockSignals( false );
}

void QgsHanaSourceSelect::cbxAllowGeometrylessTables_stateChanged( int )
{
  btnConnect_clicked();
}

void QgsHanaSourceSelect::buildQuery()
{
  setSql( mTablesTreeView->currentIndex() );
}

void QgsHanaSourceSelect::mTablesTreeView_clicked( const QModelIndex &index )
{
  mBuildQueryButton->setEnabled( index.parent().isValid() );
}

void QgsHanaSourceSelect::mTablesTreeView_doubleClicked( const QModelIndex &index )
{
  QgsSettings settings;
  if ( settings.value( QStringLiteral( "qgis/addHANADC" ), false ).toBool() )
    addButtonClicked();
  else
    setSql( index );
}

void QgsHanaSourceSelect::mSearchGroupBox_toggled( bool checked )
{
  if ( mSearchTableEdit->text().isEmpty() )
    return;

  mSearchTableEdit_textChanged( checked ? mSearchTableEdit->text() : QString( ) );
}

void QgsHanaSourceSelect::mSearchTableEdit_textChanged( const QString &text )
{
  if ( mSearchModeComboBox->currentText() == tr( "Wildcard" ) )
    mProxyModel._setFilterWildcard( text );
  else if ( mSearchModeComboBox->currentText() == tr( "RegExp" ) )
    mProxyModel._setFilterRegExp( text );
}

void QgsHanaSourceSelect::mSearchColumnComboBox_currentTextChanged( const QString &text )
{
  if ( text == tr( "All" ) )
  {
    mProxyModel.setFilterKeyColumn( -1 );
  }
  else if ( text == tr( "Schema" ) )
  {
    mProxyModel.setFilterKeyColumn( QgsHanaTableModel::DbtmSchema );
  }
  else if ( text == tr( "Table" ) )
  {
    mProxyModel.setFilterKeyColumn( QgsHanaTableModel::DbtmTable );
  }
  else if ( text == tr( "Comment" ) )
  {
    mProxyModel.setFilterKeyColumn( QgsHanaTableModel::DbtmComment );
  }
  else if ( text == tr( "Geometry column" ) )
  {
    mProxyModel.setFilterKeyColumn( QgsHanaTableModel::DbtmGeomCol );
  }
  else if ( text == tr( "Type" ) )
  {
    mProxyModel.setFilterKeyColumn( QgsHanaTableModel::DbtmGeomType );
  }
  else if ( text == tr( "Feature id" ) )
  {
    mProxyModel.setFilterKeyColumn( QgsHanaTableModel::DbtmPkCol );
  }
  else if ( text == tr( "SRID" ) )
  {
    mProxyModel.setFilterKeyColumn( QgsHanaTableModel::DbtmSrid );
  }
  else if ( text == tr( "Sql" ) )
  {
    mProxyModel.setFilterKeyColumn( QgsHanaTableModel::DbtmSql );
  }
}

void QgsHanaSourceSelect::mSearchModeComboBox_currentTextChanged( const QString &text )
{
  Q_UNUSED( text );
  mSearchTableEdit_textChanged( mSearchTableEdit->text() );
}

void QgsHanaSourceSelect::setLayerType( const QgsHanaLayerProperty &layerProperty )
{
  mTableModel.addTableEntry( mConnectionName, layerProperty );
}

QgsHanaSourceSelect::~QgsHanaSourceSelect()
{
  if ( mColumnTypeThread )
  {
    mColumnTypeThread->requestInterruption();
    mColumnTypeThread->wait();
    finishList();
  }

  QgsSettings settings;
  settings.setValue( QStringLiteral( "Windows/HanaSourceSelect/geometry" ), saveGeometry() );
  settings.setValue( QStringLiteral( "Windows/HanaSourceSelect/HoldDialogOpen" ), mHoldDialogOpen->isChecked() );

  for ( int i = 0; i < mTableModel.columnCount(); i++ )
  {
    settings.setValue( QStringLiteral( "Windows/HanaSourceSelect/columnWidths/%1" )
                       .arg( i ), mTablesTreeView->columnWidth( i ) );
  }
}

void QgsHanaSourceSelect::populateConnectionList()
{
  cmbConnections->blockSignals( true );
  cmbConnections->clear();
  cmbConnections->addItems( QgsHanaSettings::getConnectionNames() );
  cmbConnections->blockSignals( false );

  setConnectionListPosition();

  btnEdit->setDisabled( cmbConnections->count() == 0 );
  btnDelete->setDisabled( cmbConnections->count() == 0 );
  btnConnect->setDisabled( cmbConnections->count() == 0 );
  cmbConnections->setDisabled( cmbConnections->count() == 0 );
}

QStringList QgsHanaSourceSelect::selectedTables()
{
  return mSelectedTables;
}

QString QgsHanaSourceSelect::connectionInfo()
{
  return mConnectionInfo;
}

// Slot for performing action when the Add button is clicked
void QgsHanaSourceSelect::addButtonClicked()
{
  mSelectedTables.clear();

  const QModelIndexList indexes = mTablesTreeView->selectionModel()->selection().indexes();
  for ( const QModelIndex &idx : indexes )
  {
    if ( idx.column() != QgsHanaTableModel::DbtmTable )
      continue;

    QString uri = mTableModel.layerURI( mProxyModel.mapToSource( idx ), mConnectionName, mConnectionInfo );
    if ( uri.isNull() )
      continue;

    mSelectedTables << uri;
  }

  if ( mSelectedTables.empty() )
  {
    QMessageBox::information( this, tr( "Select Table" ), tr( "You must select a table in order to add a layer." ) );
  }
  else
  {
    emit addDatabaseLayers( mSelectedTables, QStringLiteral( "hana" ) );
    if ( !mHoldDialogOpen->isChecked() && widgetMode() == QgsProviderRegistry::WidgetMode::None )
      accept();
  }
}

void QgsHanaSourceSelect::btnConnect_clicked()
{
  cbxAllowGeometrylessTables->setEnabled( true );

  if ( mColumnTypeThread )
  {
    mColumnTypeThread->requestInterruption();
    mColumnTypeThread->wait();
    return;
  }

  const QString connName = cmbConnections->currentText();

  QModelIndex rootItemIndex = mTableModel.indexFromItem( mTableModel.invisibleRootItem() );
  mTableModel.removeRows( 0, mTableModel.rowCount( rootItemIndex ), rootItemIndex );

  QgsHanaSettings settings( connName, true );
  settings.setAllowGeometrylessTables( cbxAllowGeometrylessTables->isChecked() );

  const QgsDataSourceUri uri = settings.toDataSourceUri();
  bool canceled = false;

  std::unique_ptr<QgsHanaConnection> conn( QgsHanaConnection::createConnection( uri, &canceled, nullptr ) );
  if ( !conn )
  {
    if ( !canceled )
      QMessageBox::warning( this, tr( "SAP HANA" ), tr( "Unable to connect to a database" ) );
    return;
  }

  mConnectionName = connName;
  mConnectionInfo = QgsHanaUtils::connectionInfo( uri );

  QApplication::setOverrideCursor( Qt::BusyCursor );

  mColumnTypeThread = std::make_unique<QgsHanaColumnTypeThread>( mConnectionName, uri, settings.allowGeometrylessTables(), settings.userTablesOnly() );
  mColumnTypeTask = std::make_unique<QgsProxyProgressTask>( tr( "Scanning tables for %1" ).arg( mConnectionName ) );
  QgsApplication::taskManager()->addTask( mColumnTypeTask.get() );

  connect( mColumnTypeThread.get(), &QgsHanaColumnTypeThread::setLayerType,
           this, &QgsHanaSourceSelect::setLayerType );
  connect( mColumnTypeThread.get(), &QThread::finished,
           this, &QgsHanaSourceSelect::columnThreadFinished );
  connect( mColumnTypeThread.get(), &QgsHanaColumnTypeThread::progress,
           mColumnTypeTask.get(), [&]( int i, int n )
  {
    mColumnTypeTask->setProxyProgress( 100.0 * static_cast<double>( i ) / n );
  } );
  connect( mColumnTypeThread.get(), &QgsHanaColumnTypeThread::progressMessage,
           this, &QgsHanaSourceSelect::progressMessage );

  btnConnect->setText( tr( "Stop" ) );
  mColumnTypeThread->start();
}

void QgsHanaSourceSelect::finishList()
{
  QApplication::restoreOverrideCursor();

  mTablesTreeView->sortByColumn( QgsHanaTableModel::DbtmTable, Qt::AscendingOrder );
  mTablesTreeView->sortByColumn( QgsHanaTableModel::DbtmSchema, Qt::AscendingOrder );
}

void QgsHanaSourceSelect::columnThreadFinished()
{
  QString errorMsg = mColumnTypeThread->errorMessage();
  mColumnTypeThread.reset( nullptr );
  QgsProxyProgressTask *task = mColumnTypeTask.release();
  task->finalize( errorMsg.isEmpty() );
  if ( !errorMsg.isEmpty() )
    pushMessage( tr( "Failed to retrieve tables for %1" ).arg( mConnectionName ), errorMsg, Qgis::MessageLevel::Warning );

  btnConnect->setText( tr( "Connect" ) );

  finishList();
}

void QgsHanaSourceSelect::refresh()
{
  populateConnectionList();
}

void QgsHanaSourceSelect::setSql( const QModelIndex &index )
{
  if ( !index.parent().isValid() )
  {
    QgsDebugMsg( "schema item found" );
    return;
  }

  QModelIndex idx = mProxyModel.mapToSource( index );
  QString uri = mTableModel.layerURI( idx, mConnectionName, mConnectionInfo );
  if ( uri.isNull() )
  {
    QgsDebugMsg( "no uri" );
    return;
  }

  QString tableName = mTableModel.itemFromIndex( idx.sibling( idx.row(), QgsHanaTableModel::DbtmTable ) )->text();

  QgsVectorLayer vlayer( uri, tableName, QStringLiteral( "hana" ) );
  if ( !vlayer.isValid() )
    return;

  QgsQueryBuilder gb( &vlayer, this );
  if ( gb.exec() )
    mTableModel.setSql( mProxyModel.mapToSource( index ), gb.sql() );
}

QString QgsHanaSourceSelect::fullDescription(
  const QString &schema, const QString &table, const QString &column, const QString &type )
{
  QString desc;
  if ( !schema.isEmpty() )
    desc = schema + '.';
  desc += table + " (" + column + ") " + type;
  return desc;
}

void QgsHanaSourceSelect::setConnectionListPosition()
{
  QString selectedConnName = QgsHanaSettings::getSelectedConnection();
  cmbConnections->setCurrentIndex( cmbConnections->findText( selectedConnName ) );
  if ( cmbConnections->currentIndex() < 0 )
    cmbConnections->setCurrentIndex( selectedConnName.isNull() ? 0 : cmbConnections->count() - 1 );
}

void QgsHanaSourceSelect::setSearchExpression( const QString &regexp )
{
  Q_UNUSED( regexp )
}

void QgsHanaSourceSelect::treeWidgetSelectionChanged(
  const QItemSelection &selected, const QItemSelection &deselected )
{
  Q_UNUSED( deselected )
  emit enableButtons( !selected.isEmpty() );
}

void QgsHanaSourceSelect::showHelp()
{
  QgsHelp::openHelp( QStringLiteral( "managing_data_source/opening_data.html#loading-a-database-layer" ) );
}
