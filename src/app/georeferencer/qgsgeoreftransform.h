/***************************************************************************
    qgsgeoreftransform.h - Encapsulates GCP-based parameter estimation and
    reprojection for different transformation models.
     --------------------------------------
    Date                 : 18-Feb-2009
    Copyright            : (c) 2009 by Manuel Massing
    Email                : m.massing at warped-space.de
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef QGSGEOREFTRANSFORM_H
#define QGSGEOREFTRANSFORM_H

#include <gdal_alg.h>
#include "qgspoint.h"
#include "qgsgcptransformer.h"
#include "qgsrasterchangecoords.h"

/**
 * \brief Transform class for different gcp-based transform methods.
 *
 * Select transform type via \ref selectTransformParametrisation.
 * Initialize and update parameters via \ref updateParametersFromGCPs.
 * An initialized instance then provides transform functions and GDALTransformer entry points
 * for warping and coordinate remapping.
 *
 * Delegates to concrete implementations of \ref QgsGeorefInterface. For exception safety,
 * this is preferred over using the subclasses directly.
 */
class QgsGeorefTransform : public QgsGcpTransformerInterface
{
  public:

    explicit QgsGeorefTransform( TransformMethod parametrisation );
    QgsGeorefTransform();
    ~QgsGeorefTransform() override;

    /**
     * Switches the used transform type to the given parametrisation.
     */
    void selectTransformParametrisation( TransformMethod parametrisation );

    /**
     * Setting the mRasterChangeCoords for change type coordinate(map for pixel).
     */
    void setRasterChangeCoords( const QString &fileRaster );

    //! \returns Whether has Coordinate Reference Systems in image
    bool hasCrs() const { return mRasterChangeCoords.hasCrs(); }

    //! \returns Coordinates of image
    QgsPointXY toColumnLine( const QgsPointXY &pntMap ) { return mRasterChangeCoords.toColumnLine( pntMap ); }

    //! \returns Bounding box of image(transform to coordinate of Map or Image )
    QgsRectangle getBoundingBox( const QgsRectangle &rect, bool toPixel ) { return mRasterChangeCoords.getBoundingBox( rect, toPixel ); }

    //! \brief The transform parametrisation currently in use.
    TransformMethod transformParametrisation() const;

    //! True for linear, Helmert, first order polynomial
    bool providesAccurateInverseTransformation() const;

    //! \returns whether the parameters of this transform have been initialized by \ref updateParametersFromGCPs
    bool parametersInitialized() const;

    QgsGcpTransformerInterface *clone() const override;
    bool updateParametersFromGcps( const QVector<QgsPointXY> &sourceCoordinates, const QVector<QgsPointXY> &destinationCoordinates, bool invertYAxis = false ) override;
    int minimumGcpCount() const override;
    TransformMethod method() const override;
    GDALTransformerFunc GDALTransformer() const override;
    void *GDALTransformerArgs() const override;

    /**
     * \brief Transform from pixel coordinates to georeferenced coordinates.
     *
     * \note Negative y-axis points down in raster CS.
     */
    bool transformRasterToWorld( const QgsPointXY &raster, QgsPointXY &world );

    /**
     * \brief Transform from referenced coordinates to raster coordinates.
     *
     * \note Negative y-axis points down in raster CS.
     */
    bool transformWorldToRaster( const QgsPointXY &world, QgsPointXY &raster );

    /**
     * \brief Transforms from raster to world if rasterToWorld is TRUE,
     * \brief or from world to raster when rasterToWorld is FALSE.
     *
     * \note Negative y-axis points down in raster CS.
     */
    bool transform( const QgsPointXY &src, QgsPointXY &dst, bool rasterToWorld );

    //! \brief Returns origin and scale if this is a linear transform, fails otherwise.
    bool getLinearOriginScale( QgsPointXY &origin, double &scaleX, double &scaleY ) const;

    //! \brief Returns origin, scale and rotation for linear and helmert transform, fails otherwise.
    bool getOriginScaleRotation( QgsPointXY &origin, double &scaleX, double &scaleY, double &rotation ) const;

  private:
    // shallow copy constructor
    QgsGeorefTransform( const QgsGeorefTransform &other );
    QgsGeorefTransform &operator= ( const QgsGeorefTransform & ) = delete;

    // convenience wrapper around GDALTransformerFunc
    bool gdal_transform( const QgsPointXY &src, QgsPointXY &dst, int dstToSrc ) const;

    QVector<QgsPointXY> mSourceCoordinates;
    QVector<QgsPointXY> mDestinationCoordinates;
    bool mInvertYAxis = false;

    std::unique_ptr< QgsGcpTransformerInterface > mGeorefTransformImplementation;

    TransformMethod mTransformParametrisation = TransformMethod::InvalidTransform;
    bool mParametersInitialized = false;
    QgsRasterChangeCoords mRasterChangeCoords;
};

#endif //QGSGEOREFTRANSFORM_H
