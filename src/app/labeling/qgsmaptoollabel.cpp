/***************************************************************************
                          qgsmaptoollabel.cpp
                          --------------------
    begin                : 2010-11-03
    copyright            : (C) 2010 by Marco Hugentobler
    email                : marco dot hugentobler at sourcepole dot ch
***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgsmaptoollabel.h"
#include "qgsfeatureiterator.h"
#include "qgslogger.h"
#include "qgsmapcanvas.h"
#include "qgsproject.h"
#include "qgsrubberband.h"
#include "qgsvectorlayer.h"
#include "qgsvectorlayerlabeling.h"
#include "qgsdiagramrenderer.h"
#include "qgssettings.h"
#include "qgsvectorlayerjoininfo.h"
#include "qgsvectorlayerjoinbuffer.h"
#include "qgsauxiliarystorage.h"
#include "qgsgui.h"
#include "qgstextrenderer.h"
#include "qgisapp.h"
#include "qgsmapmouseevent.h"
#include "qgsadvanceddigitizingdockwidget.h"
#include "qgsstatusbar.h"

#include <QMouseEvent>

QgsMapToolLabel::QgsMapToolLabel( QgsMapCanvas *canvas, QgsAdvancedDigitizingDockWidget *cadDock )
  : QgsMapToolAdvancedDigitizing( canvas, cadDock )

{
}

QgsMapToolLabel::~QgsMapToolLabel()
{
  delete mLabelRubberBand;
  delete mHoverRubberBand;
  delete mFeatureRubberBand;
  delete mFixPointRubberBand;
}

void QgsMapToolLabel::deactivate()
{
  clearHoveredLabel();
  QgsMapToolAdvancedDigitizing::deactivate();
}

bool QgsMapToolLabel::labelAtPosition( QMouseEvent *e, QgsLabelPosition &p )
{
  QgsPointXY pt = toMapCoordinates( e->pos() );
  const QgsLabelingResults *labelingResults = mCanvas->labelingResults();
  if ( !labelingResults )
    return false;

  QList<QgsLabelPosition> labelPosList = labelingResults->labelsAtPosition( pt );
  if ( labelPosList.empty() )
    return false;

  // prioritize labels in the current selected layer, in case of overlaps
  QList<QgsLabelPosition> activeLayerLabels;
  if ( const QgsVectorLayer *currentLayer = qobject_cast< QgsVectorLayer * >( mCanvas->currentLayer() ) )
  {
    for ( const QgsLabelPosition &pos : qgis::as_const( labelPosList ) )
    {
      if ( pos.layerID == currentLayer->id() )
      {
        activeLayerLabels.append( pos );
      }
    }
  }
  if ( !activeLayerLabels.empty() )
    labelPosList = activeLayerLabels;

  // prioritize unplaced labels
  QList<QgsLabelPosition> unplacedLabels;
  for ( const QgsLabelPosition &pos : qgis::as_const( labelPosList ) )
  {
    if ( pos.isUnplaced )
    {
      unplacedLabels.append( pos );
    }
  }
  if ( !unplacedLabels.empty() )
    labelPosList = unplacedLabels;

  if ( labelPosList.count() > 1 )
  {
    // multiple candidates found, so choose the smallest (i.e. most difficult to select otherwise)
    double minSize = std::numeric_limits< double >::max();
    for ( const QgsLabelPosition &pos : qgis::as_const( labelPosList ) )
    {
      const double labelSize = pos.width * pos.height;
      if ( labelSize < minSize )
      {
        minSize = labelSize;
        p = pos;
      }
    }
  }
  else
  {
    // only one candidate
    p = labelPosList.at( 0 );
  }

  return true;
}

void QgsMapToolLabel::createRubberBands()
{
  delete mLabelRubberBand;
  delete mFeatureRubberBand;

  //label rubber band
  mLabelRubberBand = new QgsRubberBand( mCanvas, QgsWkbTypes::LineGeometry );
  mLabelRubberBand->addPoint( mCurrentLabel.pos.cornerPoints.at( 0 ) );
  mLabelRubberBand->addPoint( mCurrentLabel.pos.cornerPoints.at( 1 ) );
  mLabelRubberBand->addPoint( mCurrentLabel.pos.cornerPoints.at( 2 ) );
  mLabelRubberBand->addPoint( mCurrentLabel.pos.cornerPoints.at( 3 ) );
  mLabelRubberBand->addPoint( mCurrentLabel.pos.cornerPoints.at( 0 ) );
  mLabelRubberBand->setColor( QColor( 0, 255, 0, 65 ) );
  mLabelRubberBand->setWidth( 3 );
  mLabelRubberBand->show();

  //feature rubber band
  QgsVectorLayer *vlayer = mCurrentLabel.layer;
  if ( vlayer )
  {
    QgsFeature f;
    if ( currentFeature( f, true ) )
    {
      QgsGeometry geom = f.geometry();
      if ( !geom.isNull() )
      {
        if ( geom.type() == QgsWkbTypes::PolygonGeometry )
        {
          // for polygons, we don't want to fill the whole polygon itself with the rubber band
          // as that obscures too much of the map and prevents users from getting a good view of
          // the underlying map
          // instead, just use the boundary of the polygon for the rubber band
          geom = QgsGeometry( geom.constGet()->boundary() );
        }
        QgsSettings settings;
        int r = settings.value( QStringLiteral( "qgis/digitizing/line_color_red" ), 255 ).toInt();
        int g = settings.value( QStringLiteral( "qgis/digitizing/line_color_green" ), 0 ).toInt();
        int b = settings.value( QStringLiteral( "qgis/digitizing/line_color_blue" ), 0 ).toInt();
        int a = settings.value( QStringLiteral( "qgis/digitizing/line_color_alpha" ), 200 ).toInt();
        mFeatureRubberBand = new QgsRubberBand( mCanvas, geom.type() );
        mFeatureRubberBand->setColor( QColor( r, g, b, a ) );
        mFeatureRubberBand->setToGeometry( geom, vlayer );
        mFeatureRubberBand->show();
      }
    }

    //fixpoint rubber band
    QgsPointXY fixPoint;
    if ( currentLabelRotationPoint( fixPoint, false, false ) )
    {
      if ( mCanvas )
      {
        const QgsMapSettings &s = mCanvas->mapSettings();
        fixPoint = s.mapToLayerCoordinates( vlayer, fixPoint );
      }

      QgsGeometry pointGeom = QgsGeometry::fromPointXY( fixPoint );
      mFixPointRubberBand = new QgsRubberBand( mCanvas, QgsWkbTypes::LineGeometry );
      mFixPointRubberBand->setColor( QColor( 0, 0, 255, 65 ) );
      mFixPointRubberBand->setToGeometry( pointGeom, vlayer );
      mFixPointRubberBand->show();
    }
  }
}

void QgsMapToolLabel::deleteRubberBands()
{
  delete mLabelRubberBand;
  mLabelRubberBand = nullptr;
  delete mFeatureRubberBand;
  mFeatureRubberBand = nullptr;
  delete mFixPointRubberBand;
  mFixPointRubberBand = nullptr;
  cadDockWidget()->clear();
  cadDockWidget()->clearPoints();
}

QString QgsMapToolLabel::currentLabelText( int trunc )
{
  if ( !mCurrentLabel.valid )
  {
    return QString();
  }
  QgsPalLayerSettings &labelSettings = mCurrentLabel.settings;

  if ( labelSettings.isExpression )
  {
    QString labelText = mCurrentLabel.pos.labelText;

    if ( trunc > 0 && labelText.length() > trunc )
    {
      labelText.truncate( trunc );
      labelText += QChar( 0x2026 );
    }
    return labelText;
  }
  else
  {
    QgsVectorLayer *vlayer = mCurrentLabel.layer;
    if ( !vlayer )
    {
      return QString();
    }

    QString labelField = labelSettings.fieldName;
    if ( !labelField.isEmpty() )
    {
      int labelFieldId = vlayer->fields().lookupField( labelField );
      QgsFeature f;
      if ( vlayer->getFeatures( QgsFeatureRequest().setFilterFid( mCurrentLabel.pos.featureId ).setFlags( QgsFeatureRequest::NoGeometry ) ).nextFeature( f ) )
      {
        QString labelText = f.attribute( labelFieldId ).toString();
        if ( trunc > 0 && labelText.length() > trunc )
        {
          labelText.truncate( trunc );
          labelText += QChar( 0x2026 );
        }
        return labelText;
      }
    }
  }
  return QString();
}

void QgsMapToolLabel::currentAlignment( QString &hali, QString &vali )
{
  hali = QStringLiteral( "Left" );
  vali = QStringLiteral( "Bottom" );

  QgsVectorLayer *vlayer = mCurrentLabel.layer;
  if ( !vlayer )
  {
    return;
  }

  QgsFeature f;
  if ( !currentFeature( f ) )
  {
    return;
  }

  hali = evaluateDataDefinedProperty( QgsPalLayerSettings::Hali, mCurrentLabel.settings, f, hali ).toString();
  vali = evaluateDataDefinedProperty( QgsPalLayerSettings::Vali, mCurrentLabel.settings, f, vali ).toString();
}

bool QgsMapToolLabel::currentFeature( QgsFeature &f, bool fetchGeom )
{
  QgsVectorLayer *vlayer = mCurrentLabel.layer;
  if ( !vlayer )
  {
    return false;
  }
  return vlayer->getFeatures( QgsFeatureRequest()
                              .setFilterFid( mCurrentLabel.pos.featureId )
                              .setFlags( fetchGeom ? QgsFeatureRequest::NoFlags : QgsFeatureRequest::NoGeometry )
                            ).nextFeature( f );
}

QFont QgsMapToolLabel::currentLabelFont()
{
  QFont font;

  QgsPalLayerSettings &labelSettings = mCurrentLabel.settings;
  QgsVectorLayer *vlayer = mCurrentLabel.layer;

  QgsRenderContext context = QgsRenderContext::fromMapSettings( mCanvas->mapSettings() );
  if ( mCurrentLabel.valid && vlayer )
  {
    font = labelSettings.format().font();

    QgsFeature f;
    if ( vlayer->getFeatures( QgsFeatureRequest().setFilterFid( mCurrentLabel.pos.featureId ).setFlags( QgsFeatureRequest::NoGeometry ) ).nextFeature( f ) )
    {
      //size
      int sizeIndx = dataDefinedColumnIndex( QgsPalLayerSettings::Size, mCurrentLabel.settings, vlayer );
      if ( sizeIndx != -1 )
      {
        font.setPixelSize( QgsTextRenderer::sizeToPixel( f.attribute( sizeIndx ).toDouble(),
                           context, labelSettings.format().sizeUnit(),
                           labelSettings.format().sizeMapUnitScale() ) );
      }

      //family
      int fmIndx = dataDefinedColumnIndex( QgsPalLayerSettings::Family, labelSettings, vlayer );
      if ( fmIndx != -1 )
      {
        font.setFamily( f.attribute( fmIndx ).toString() );
      }

      //underline
      int ulIndx = dataDefinedColumnIndex( QgsPalLayerSettings::Underline, labelSettings, vlayer );
      if ( ulIndx != -1 )
      {
        font.setUnderline( f.attribute( ulIndx ).toBool() );
      }

      //strikeout
      int soIndx = dataDefinedColumnIndex( QgsPalLayerSettings::Strikeout, labelSettings, vlayer );
      if ( soIndx != -1 )
      {
        font.setStrikeOut( f.attribute( soIndx ).toBool() );
      }

      //bold
      int boIndx = dataDefinedColumnIndex( QgsPalLayerSettings::Bold, labelSettings, vlayer );
      if ( boIndx != -1 )
      {
        font.setBold( f.attribute( boIndx ).toBool() );
      }

      //italic
      int itIndx = dataDefinedColumnIndex( QgsPalLayerSettings::Italic, labelSettings, vlayer );
      if ( itIndx != -1 )
      {
        font.setItalic( f.attribute( itIndx ).toBool() );
      }

      // TODO: Add other font data defined values (word spacing, etc.)
    }
  }

  return font;
}

bool QgsMapToolLabel::currentLabelPreserveRotation()
{
  if ( mCurrentLabel.valid )
  {
    return mCurrentLabel.settings.preserveRotation;
  }

  return true; // default, so there is no accidental data loss
}

bool QgsMapToolLabel::currentLabelRotationPoint( QgsPointXY &pos, bool ignoreUpsideDown, bool rotatingUnpinned )
{
  QVector<QgsPointXY> cornerPoints = mCurrentLabel.pos.cornerPoints;
  if ( cornerPoints.size() < 4 )
  {
    return false;
  }

  if ( mCurrentLabel.pos.upsideDown && !ignoreUpsideDown )
  {
    pos = cornerPoints.at( 2 );
  }
  else
  {
    pos = cornerPoints.at( 0 );
  }

  //alignment always center/center and rotation 0 for diagrams
  if ( mCurrentLabel.pos.isDiagram )
  {
    pos.setX( pos.x() + mCurrentLabel.pos.labelRect.width() / 2.0 );
    pos.setY( pos.y() + mCurrentLabel.pos.labelRect.height() / 2.0 );
    return true;
  }

  //adapt pos depending on data defined alignment
  QString haliString, valiString;
  currentAlignment( haliString, valiString );

  // rotate unpinned labels (i.e. no hali/vali settings) as if hali/vali was Center/Half
  if ( rotatingUnpinned )
  {
    haliString = QStringLiteral( "Center" );
    valiString = QStringLiteral( "Half" );
  }

//  QFont labelFont = labelFontCurrentFeature();
  QFontMetricsF labelFontMetrics( mCurrentLabel.pos.labelFont );

  // NOTE: this assumes the label corner points comprise a rectangle and that the
  //       CRS supports equidistant measurements to accurately determine hypotenuse
  QgsPointXY cp_0 = cornerPoints.at( 0 );
  QgsPointXY cp_1 = cornerPoints.at( 1 );
  QgsPointXY cp_3 = cornerPoints.at( 3 );
  //  QgsDebugMsg( QStringLiteral( "cp_0: x=%1, y=%2" ).arg( cp_0.x() ).arg( cp_0.y() ) );
  //  QgsDebugMsg( QStringLiteral( "cp_1: x=%1, y=%2" ).arg( cp_1.x() ).arg( cp_1.y() ) );
  //  QgsDebugMsg( QStringLiteral( "cp_3: x=%1, y=%2" ).arg( cp_3.x() ).arg( cp_3.y() ) );
  double labelSizeX = std::sqrt( cp_0.sqrDist( cp_1 ) );
  double labelSizeY = std::sqrt( cp_0.sqrDist( cp_3 ) );

  double xdiff = 0;
  double ydiff = 0;

  if ( haliString.compare( QLatin1String( "Center" ), Qt::CaseInsensitive ) == 0 )
  {
    xdiff = labelSizeX / 2.0;
  }
  else if ( haliString.compare( QLatin1String( "Right" ), Qt::CaseInsensitive ) == 0 )
  {
    xdiff = labelSizeX;
  }

  if ( valiString.compare( QLatin1String( "Top" ), Qt::CaseInsensitive ) == 0 || valiString.compare( QLatin1String( "Cap" ), Qt::CaseInsensitive ) == 0 )
  {
    ydiff = labelSizeY;
  }
  else
  {
    double descentRatio = 1 / labelFontMetrics.ascent() / labelFontMetrics.height();
    if ( valiString.compare( QLatin1String( "Base" ), Qt::CaseInsensitive ) == 0 )
    {
      ydiff = labelSizeY * descentRatio;
    }
    else if ( valiString.compare( QLatin1String( "Half" ), Qt::CaseInsensitive ) == 0 )
    {
      ydiff = labelSizeY * 0.5 * ( 1 - descentRatio );
    }
  }

  double angle = mCurrentLabel.pos.rotation;
  double xd = xdiff * std::cos( angle ) - ydiff * std::sin( angle );
  double yd = xdiff * std::sin( angle ) + ydiff * std::cos( angle );
  if ( mCurrentLabel.pos.upsideDown && !ignoreUpsideDown )
  {
    pos.setX( pos.x() - xd );
    pos.setY( pos.y() - yd );
  }
  else
  {
    pos.setX( pos.x() + xd );
    pos.setY( pos.y() + yd );
  }
  return true;
}

#if 0
bool QgsMapToolLabel::hasDataDefinedColumn( QgsPalLayerSettings::DataDefinedProperties p, QgsVectorLayer *vlayer ) const
{
  const auto constSubProviders = vlayer->labeling()->subProviders();
  for ( const QString &providerId : constSubProviders )
  {
    if ( QgsPalLayerSettings *settings = vlayer->labeling()->settings( vlayer, providerId ) )
    {
      QString fieldname = dataDefinedColumnName( p, *settings );
      if ( !fieldname.isEmpty() )
        return true;
    }
  }
  return false;
}
#endif

QString QgsMapToolLabel::dataDefinedColumnName( QgsPalLayerSettings::Property p, const QgsPalLayerSettings &labelSettings ) const
{
  if ( !labelSettings.dataDefinedProperties().isActive( p ) )
    return QString();

  QgsProperty prop = labelSettings.dataDefinedProperties().property( p );
  if ( prop.propertyType() != QgsProperty::FieldBasedProperty )
    return QString();

  return prop.field();
}

int QgsMapToolLabel::dataDefinedColumnIndex( QgsPalLayerSettings::Property p, const QgsPalLayerSettings &labelSettings, const QgsVectorLayer *vlayer ) const
{
  QString fieldname = dataDefinedColumnName( p, labelSettings );
  if ( !fieldname.isEmpty() )
    return vlayer->fields().lookupField( fieldname );
  return -1;
}

QVariant QgsMapToolLabel::evaluateDataDefinedProperty( QgsPalLayerSettings::Property property, const QgsPalLayerSettings &labelSettings, const QgsFeature &feature, const QVariant &defaultValue ) const
{
  QgsExpressionContext context = mCanvas->mapSettings().expressionContext();
  context.setFeature( feature );
  context.setFields( feature.fields() );
  return labelSettings.dataDefinedProperties().value( property, context, defaultValue );
}

bool QgsMapToolLabel::currentLabelDataDefinedPosition( double &x, bool &xSuccess, double &y, bool &ySuccess, int &xCol, int &yCol ) const
{
  QgsVectorLayer *vlayer = mCurrentLabel.layer;
  QgsFeatureId featureId = mCurrentLabel.pos.featureId;

  xSuccess = false;
  ySuccess = false;

  if ( !vlayer )
  {
    return false;
  }

  if ( mCurrentLabel.pos.isDiagram )
  {
    if ( !diagramMoveable( vlayer, xCol, yCol ) )
    {
      return false;
    }
  }
  else if ( !labelMoveable( vlayer, mCurrentLabel.settings, xCol, yCol ) )
  {
    return false;
  }

  QgsFeature f;
  if ( !vlayer->getFeatures( QgsFeatureRequest().setFilterFid( featureId ).setFlags( QgsFeatureRequest::NoGeometry ) ).nextFeature( f ) )
  {
    return false;
  }

  if ( mCurrentLabel.pos.isUnplaced )
  {
    xSuccess = false;
    ySuccess = false;
  }
  else
  {
    QgsAttributes attributes = f.attributes();
    if ( !attributes.at( xCol ).isNull() )
      x = attributes.at( xCol ).toDouble( &xSuccess );
    if ( !attributes.at( yCol ).isNull() )
      y = attributes.at( yCol ).toDouble( &ySuccess );
  }

  return true;
}

bool QgsMapToolLabel::layerIsRotatable( QgsVectorLayer *vlayer, int &rotationCol ) const
{
  if ( !vlayer || !vlayer->isEditable() || !vlayer->labelsEnabled() )
  {
    return false;
  }

  const auto constSubProviders = vlayer->labeling()->subProviders();
  for ( const QString &providerId : constSubProviders )
  {
    if ( labelIsRotatable( vlayer, vlayer->labeling()->settings( providerId ), rotationCol ) )
      return true;
  }

  return false;
}

bool QgsMapToolLabel::labelIsRotatable( QgsVectorLayer *layer, const QgsPalLayerSettings &settings, int &rotationCol ) const
{
  QString rColName = dataDefinedColumnName( QgsPalLayerSettings::LabelRotation, settings );
  rotationCol = layer->fields().lookupField( rColName );
  return rotationCol != -1;
}


bool QgsMapToolLabel::currentLabelDataDefinedRotation( double &rotation, bool &rotationSuccess, int &rCol, bool ignoreXY ) const
{
  QgsVectorLayer *vlayer = mCurrentLabel.layer;
  QgsFeatureId featureId = mCurrentLabel.pos.featureId;

  rotationSuccess = false;
  if ( !vlayer )
  {
    return false;
  }

  if ( !labelIsRotatable( vlayer, mCurrentLabel.settings, rCol ) )
  {
    return false;
  }

  QgsFeature f;
  if ( !vlayer->getFeatures( QgsFeatureRequest().setFilterFid( featureId ).setFlags( QgsFeatureRequest::NoGeometry ) ).nextFeature( f ) )
  {
    return false;
  }

  //test, if data defined x- and y- values are not null. Otherwise, the position is determined by PAL and the rotation cannot be fixed
  if ( !ignoreXY )
  {
    int xCol, yCol;
    double x, y;
    bool xSuccess, ySuccess;
    if ( !currentLabelDataDefinedPosition( x, xSuccess, y, ySuccess, xCol, yCol ) || !xSuccess || !ySuccess )
    {
      return false;
    }
  }

  rotation = f.attribute( rCol ).toDouble( &rotationSuccess );
  return true;
}

bool QgsMapToolLabel::dataDefinedShowHide( QgsVectorLayer *vlayer, QgsFeatureId featureId, int &show, bool &showSuccess, int &showCol ) const
{
  showSuccess = false;
  if ( !vlayer )
  {
    return false;
  }

  if ( mCurrentLabel.pos.isDiagram )
  {
    if ( ! diagramCanShowHide( vlayer, showCol ) )
    {
      return false;
    }
  }
  else if ( ! labelCanShowHide( vlayer, showCol ) )
  {
    return false;
  }

  QgsFeature f;
  if ( !vlayer->getFeatures( QgsFeatureRequest().setFilterFid( featureId ).setFlags( QgsFeatureRequest::NoGeometry ) ).nextFeature( f ) )
  {
    return false;
  }

  show = f.attribute( showCol ).toInt( &showSuccess );
  return true;
}

bool QgsMapToolLabel::diagramMoveable( QgsVectorLayer *vlayer, int &xCol, int &yCol ) const
{
  if ( vlayer && vlayer->diagramsEnabled() )
  {
    const QgsDiagramLayerSettings *dls = vlayer->diagramLayerSettings();
    if ( dls )
    {
      xCol = -1;
      if ( QgsProperty ddX = dls->dataDefinedProperties().property( QgsDiagramLayerSettings::PositionX ) )
      {
        if ( ddX.propertyType() == QgsProperty::FieldBasedProperty && ddX.isActive() )
        {
          xCol = vlayer->fields().lookupField( ddX.field() );
        }
      }
      yCol = -1;
      if ( QgsProperty ddY = dls->dataDefinedProperties().property( QgsDiagramLayerSettings::PositionY ) )
      {
        if ( ddY.propertyType() == QgsProperty::FieldBasedProperty && ddY.isActive() )
        {
          yCol = vlayer->fields().lookupField( ddY.field() );
        }
      }
      return xCol >= 0 && yCol >= 0;
    }
  }
  return false;
}

bool QgsMapToolLabel::labelMoveable( QgsVectorLayer *vlayer, int &xCol, int &yCol ) const
{
  if ( !vlayer || !vlayer->isEditable() || !vlayer->labelsEnabled() )
  {
    return false;
  }

  const auto constSubProviders = vlayer->labeling()->subProviders();
  for ( const QString &providerId : constSubProviders )
  {
    if ( labelMoveable( vlayer, vlayer->labeling()->settings( providerId ), xCol, yCol ) )
      return true;
  }

  return false;
}

bool QgsMapToolLabel::labelMoveable( QgsVectorLayer *vlayer, const QgsPalLayerSettings &settings, int &xCol, int &yCol ) const
{
  QString xColName = dataDefinedColumnName( QgsPalLayerSettings::PositionX, settings );
  QString yColName = dataDefinedColumnName( QgsPalLayerSettings::PositionY, settings );
  //return !xColName.isEmpty() && !yColName.isEmpty();
  xCol = vlayer->fields().lookupField( xColName );
  yCol = vlayer->fields().lookupField( yColName );
  return ( xCol != -1 && yCol != -1 );
}

bool QgsMapToolLabel::layerCanPin( QgsVectorLayer *vlayer, int &xCol, int &yCol ) const
{
  // currently same as QgsMapToolLabel::labelMoveable, but may change
  bool canPin = labelMoveable( vlayer, xCol, yCol );
  return canPin;
}

bool QgsMapToolLabel::labelCanShowHide( QgsVectorLayer *vlayer, int &showCol ) const
{
  if ( !vlayer || !vlayer->isEditable() || !vlayer->labelsEnabled() )
  {
    return false;
  }

  const auto constSubProviders = vlayer->labeling()->subProviders();
  for ( const QString &providerId : constSubProviders )
  {
    QString fieldname = dataDefinedColumnName( QgsPalLayerSettings::Show,
                        vlayer->labeling()->settings( providerId ) );
    showCol = vlayer->fields().lookupField( fieldname );
    if ( showCol != -1 )
      return true;
  }

  return false;
}

bool QgsMapToolLabel::isPinned()
{
  bool rc = false;

  if ( ! mCurrentLabel.pos.isDiagram )
  {
    rc = mCurrentLabel.pos.isPinned;
  }
  else
  {
    // for diagrams, the isPinned attribute is not set. So we check directly if
    // there's data defined.
    int xCol, yCol;
    double x, y;
    bool xSuccess, ySuccess;

    if ( currentLabelDataDefinedPosition( x, xSuccess, y, ySuccess, xCol, yCol ) && xSuccess && ySuccess )
      rc = true;
  }

  return rc;
}

bool QgsMapToolLabel::diagramCanShowHide( QgsVectorLayer *vlayer, int &showCol ) const
{
  showCol = -1;

  if ( vlayer && vlayer->isEditable() && vlayer->diagramsEnabled() )
  {
    if ( const QgsDiagramLayerSettings *dls = vlayer->diagramLayerSettings() )
    {
      if ( QgsProperty ddShow = dls->dataDefinedProperties().property( QgsDiagramLayerSettings::Show ) )
      {
        if ( ddShow.propertyType() == QgsProperty::FieldBasedProperty && ddShow.isActive() )
        {
          showCol = vlayer->fields().lookupField( ddShow.field() );
        }
      }
    }
  }

  return showCol >= 0;
}

//

QgsMapToolLabel::LabelDetails::LabelDetails( const QgsLabelPosition &p )
  : pos( p )
{
  layer = QgsProject::instance()->mapLayer<QgsVectorLayer *>( pos.layerID );
  if ( layer && layer->labelsEnabled() && !p.isDiagram )
  {
    settings = layer->labeling()->settings( pos.providerID );
    valid = true;
  }
  else if ( layer && layer->diagramsEnabled() && p.isDiagram )
  {
    valid = true;
  }

  if ( !valid )
  {
    layer = nullptr;
    settings = QgsPalLayerSettings();
  }
}

bool QgsMapToolLabel::createAuxiliaryFields( QgsPalIndexes &indexes, bool overwriteExpression )
{
  return createAuxiliaryFields( mCurrentLabel, indexes, overwriteExpression );
}

bool QgsMapToolLabel::createAuxiliaryFields( LabelDetails &details, QgsPalIndexes &indexes, bool overwriteExpression ) const
{
  bool newAuxiliaryLayer = false;
  QgsVectorLayer *vlayer = details.layer;
  QString providerId = details.pos.providerID;

  if ( !vlayer || !vlayer->labelsEnabled() )
    return false;

  if ( !vlayer->auxiliaryLayer() )
  {
    QgsNewAuxiliaryLayerDialog dlg( vlayer );
    dlg.exec();
    newAuxiliaryLayer = true;
  }

  if ( !vlayer->auxiliaryLayer() )
    return false;

  QgsTemporaryCursorOverride cursor( Qt::WaitCursor );
  bool changed = false;
  for ( const QgsPalLayerSettings::Property &p : qgis::as_const( mPalProperties ) )
  {
    int index = -1;

    // always use the default activated property
    QgsProperty prop = details.settings.dataDefinedProperties().property( p );
    if ( prop.propertyType() == QgsProperty::FieldBasedProperty && prop.isActive() )
    {
      index = vlayer->fields().lookupField( prop.field() );
    }
    else if ( prop.propertyType() != QgsProperty::ExpressionBasedProperty || overwriteExpression )
    {
      index = QgsAuxiliaryLayer::createProperty( p, vlayer );
      changed = true;
    }

    indexes[p] = index;
  }
  if ( changed )
    emit vlayer->styleChanged();

  details.settings = vlayer->labeling()->settings( providerId );

  return newAuxiliaryLayer;
}

bool QgsMapToolLabel::createAuxiliaryFields( QgsDiagramIndexes &indexes, bool overwriteExpression )
{
  return createAuxiliaryFields( mCurrentLabel, indexes, overwriteExpression );
}


bool QgsMapToolLabel::createAuxiliaryFields( LabelDetails &details, QgsDiagramIndexes &indexes, bool overwriteExpression )
{
  bool newAuxiliaryLayer = false;
  QgsVectorLayer *vlayer = details.layer;

  if ( !vlayer )
    return newAuxiliaryLayer;

  if ( !vlayer->auxiliaryLayer() )
  {
    QgsNewAuxiliaryLayerDialog dlg( vlayer );
    dlg.exec();
    newAuxiliaryLayer = true;
  }

  if ( !vlayer->auxiliaryLayer() )
    return false;

  QgsTemporaryCursorOverride cursor( Qt::WaitCursor );
  bool changed = false;
  for ( const QgsDiagramLayerSettings::Property &p : qgis::as_const( mDiagramProperties ) )
  {
    int index = -1;

    // always use the default activated property
    QgsProperty prop = vlayer->diagramLayerSettings()->dataDefinedProperties().property( p );
    if ( prop.propertyType() == QgsProperty::FieldBasedProperty && prop.isActive() )
    {
      index = vlayer->fields().lookupField( prop.field() );
    }
    else if ( prop.propertyType() != QgsProperty::ExpressionBasedProperty || overwriteExpression )
    {
      index = QgsAuxiliaryLayer::createProperty( p, vlayer );
      changed = true;
    }

    indexes[p] = index;
  }
  if ( changed )
    emit vlayer->styleChanged();

  return newAuxiliaryLayer;
}

void QgsMapToolLabel::updateHoveredLabel( QgsMapMouseEvent *e )
{
  if ( !mHoverRubberBand )
  {
    mHoverRubberBand = new QgsRubberBand( mCanvas, QgsWkbTypes::LineGeometry );
    mHoverRubberBand->setWidth( 2 );
    mHoverRubberBand->setSecondaryStrokeColor( QColor( 255, 255, 255, 100 ) );
    mHoverRubberBand->setColor( QColor( 200, 0, 120, 255 ) );
  }
  QgsLabelPosition labelPos;
  if ( !labelAtPosition( e, labelPos ) )
  {
    mHoverRubberBand->hide();
    mCurrentHoverLabel = LabelDetails();
    return;
  }

  LabelDetails newHoverLabel( labelPos );

  if ( mCurrentHoverLabel.valid &&
       newHoverLabel.layer == mCurrentHoverLabel.layer &&
       newHoverLabel.pos.featureId == mCurrentHoverLabel.pos.featureId &&
       newHoverLabel.pos.providerID == mCurrentHoverLabel.pos.providerID
     )
    return;

  if ( !canModifyLabel( newHoverLabel ) )
  {
    mHoverRubberBand->hide();
    mCurrentHoverLabel = LabelDetails();
    return;
  }

  mCurrentHoverLabel = newHoverLabel;

  mHoverRubberBand->show();
  mHoverRubberBand->reset( QgsWkbTypes::LineGeometry );
  mHoverRubberBand->addPoint( labelPos.cornerPoints.at( 0 ) );
  mHoverRubberBand->addPoint( labelPos.cornerPoints.at( 1 ) );
  mHoverRubberBand->addPoint( labelPos.cornerPoints.at( 2 ) );
  mHoverRubberBand->addPoint( labelPos.cornerPoints.at( 3 ) );
  mHoverRubberBand->addPoint( labelPos.cornerPoints.at( 0 ) );
  QgisApp::instance()->statusBarIface()->showMessage( tr( "Label “%1” in %2" ).arg( labelPos.labelText, mCurrentHoverLabel.layer->name() ), 2000 );
}

void QgsMapToolLabel::clearHoveredLabel()
{
  if ( mHoverRubberBand )
    mHoverRubberBand->hide();
  mCurrentHoverLabel = LabelDetails();
}

bool QgsMapToolLabel::canModifyLabel( const QgsMapToolLabel::LabelDetails & )
{
  return true;
}
