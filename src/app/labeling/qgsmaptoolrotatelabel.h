/***************************************************************************
                          qgsmaptoolrotatelabel.h
                          -----------------------
    begin                : 2010-11-09
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

#ifndef QGSMAPTOOLROTATELABEL_H
#define QGSMAPTOOLROTATELABEL_H

#include "qgsmaptoollabel.h"
#include "qgis_app.h"
class QgsPointRotationItem;

class APP_EXPORT QgsMapToolRotateLabel: public QgsMapToolLabel
{
    Q_OBJECT

  public:
    QgsMapToolRotateLabel( QgsMapCanvas *canvas, QgsAdvancedDigitizingDockWidget *cadDock );
    ~QgsMapToolRotateLabel() override;

    void canvasMoveEvent( QgsMapMouseEvent *e ) override;
    void canvasPressEvent( QgsMapMouseEvent *e ) override;
    void keyReleaseEvent( QKeyEvent *e ) override;

  protected:

    bool canModifyLabel( const LabelDetails &label ) override;

    static int roundTo15Degrees( double n );
    //! Converts azimuth value so that 0 is corresponds to East
    static double convertAzimuth( double a );

    QgsRubberBand *createRotationPreviewBox();
    void setRotationPreviewBox( double rotation );

    //! Rotates input point clockwise around centerPoint
    QgsPointXY rotatePointClockwise( const QgsPointXY &input, const QgsPointXY &centerPoint, double degrees ) const;

    double mStartRotation; //rotation value prior to start rotating
    double mCurrentRotation;
    double mCurrentMouseAzimuth;
    QgsPointXY mRotationPoint;
    QgsPointRotationItem *mRotationItem = nullptr;
    QgsRubberBand *mRotationPreviewBox = nullptr;

    //! True if ctrl was pressed during the last mouse move event
    bool mCtrlPressed;
};

#endif // QGSMAPTOOLROTATELABEL_H
