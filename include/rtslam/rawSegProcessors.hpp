/**
 * \file rawProcessors.hpp
 *
 *  Some wrappers to raw processors to be used generically
 *
 * \date 20/07/2010
 * \author croussil@laas.fr
 *
 * ## Add detailed description here ##
 *
 * \ingroup rtslam
 */

#ifndef RAWSEGPROCESSORS_HPP
#define RAWSEGPROCESSORS_HPP


#include "dseg/DirectSegmentsTracker.hpp"
#include "dseg/SegmentHypothesis.hpp"
#include "dseg/SegmentsSet.hpp"
#include "dseg/ConstantVelocityPredictor.hpp"
#include "dseg/RtslamPredictor.hpp"
#include "rtslam/hierarchicalDirectSegmentDetector.hpp"

#include "rtslam/rawImage.hpp"
#include "rtslam/sensorPinHole.hpp"
#include "rtslam/descriptorImageSeg.hpp"

namespace jafar {
namespace rtslam {

   class DsegMatcher
   {
      private:
         dseg::DirectSegmentsTracker matcher;
         dseg::RtslamPredictor predictor;

      public:
         struct matcher_params_t {
            // RANSAC
            int maxSearchSize;
            double lowInnov;      ///<     search region radius for first RANSAC consensus
            double threshold;     ///<     matching threshold
            double mahalanobisTh; ///< Mahalanobis distance for outlier rejection
            double relevanceTh; ///< Mahalanobis distance for no information rejection
            double measStd;       ///<       measurement noise std deviation
            double measVar;       ///<       measurement noise variance
         } params;

      private :
         void projectExtremities(const vec& meas, const vec& exp, vec& newMeas) const
         {
            // extract predicted points
            vec2 P1 = subrange(exp,0,2);
            vec2 P2 = subrange(exp,2,4);
            // extract measured line
            vec2 L1 = subrange(meas,0,2);
            vec2 L2 = subrange(meas,2,4);
            // TODO : be carefull L1 != L2
            // project predicted points on line
            double u = (((P1(0) - L1(0))+(L2(0) - L1(0)))
                       +((P1(1) - L1(1))+(L2(1) - L1(1))))
                       /(norm_1(L1 - L2) * norm_1(L1 - L2));
            subrange(newMeas,0,2) = L1 + u*(L2 - L1);

            u = (((P2(0) - L1(0))+(L2(0) - L1(0)))
                +((P2(1) - L1(1))+(L2(1) - L1(1))))
                /(norm_1(L1 - L2) * norm_1(L1 - L2));
            subrange(newMeas,2,4) = L1 + u*(L2 - L1);
         }

      public:
         DsegMatcher(double lowInnov, double threshold, double mahalanobisTh, double relevanceTh, double measStd):
            matcher(), predictor()
         {
            params.lowInnov = lowInnov;
            params.threshold = threshold;
            params.mahalanobisTh = mahalanobisTh;
            params.relevanceTh = relevanceTh;
            params.measStd = measStd;
         }

         void match(const boost::shared_ptr<RawImage> & rawPtr, const appearance_ptr_t & targetApp, const image::ConvexRoi & roi, Measurement & measure, appearance_ptr_t & app)
         {
            app_seg_ptr_t targetAppSpec = SPTR_CAST<AppearanceSegment>(targetApp);
            app_seg_ptr_t appSpec = SPTR_CAST<AppearanceSegment>(app);

            dseg::SegmentsSet setin, setout;

            setin.addSegment(targetAppSpec->hypothesis());
            matcher.trackSegment(*(rawPtr->img),setin,&predictor,setout);

            if(setout.count() > 0) {
               measure.std(params.measStd);
               measure.x(0) = setout.segmentAt(0)->x1();
               measure.x(1) = setout.segmentAt(0)->y1();
               measure.x(2) = setout.segmentAt(0)->x2();
               measure.x(3) = setout.segmentAt(0)->y2();
               measure.matchScore = 1;
               appSpec->setHypothesis(setout.segmentAt(0));
            }
            else {
               measure.matchScore = 0;
            }
         }
   };

   class HDsegDetector
   {
      private:
         HierarchicalDirectSegmentDetector detector;
         boost::shared_ptr<DescriptorFactoryAbstract> descFactory;

      public:
         struct detector_params_t {
            // RANSAC
            double measStd;       ///<       measurement noise std deviation
            double measVar;       ///<       measurement noise variance
            // HDSEG
            int hierarchyLevel;
         } params;

      public:
         HDsegDetector(int hierarchyLevel, double measStd,
            boost::shared_ptr<DescriptorFactoryAbstract> const &descFactory):
            detector(), descFactory(descFactory)
         {
            params.hierarchyLevel = hierarchyLevel;
            params.measStd = measStd;
            params.measVar = measStd * measStd;
         }

         bool detect(const boost::shared_ptr<RawImage> & rawData, const image::ConvexRoi &roi, boost::shared_ptr<FeatureSegment> & featPtr)
         {
            bool ret = false;
            featPtr.reset(new FeatureSegment());
            featPtr->measurement.std(params.measStd);

            ret = detector.detectIn(*(rawData->img.get()), featPtr, &roi);

            if(ret)
            {
               // Don't extract yet

               /*
               // extract appearance
               vec pix = featPtr->measurement.x();
               boost::shared_ptr<AppearanceImagePoint> appPtr = SPTR_CAST<AppearanceImagePoint>(featPtr->appearancePtr);
               rawData->img->extractPatch(appPtr->patch, (int)pix(0), (int)pix(1), params.patchSize, params.patchSize);
               appPtr->offset.x()(0) = pix(0) - ((int)pix(0) + 0.5);
               appPtr->offset.x()(1) = pix(1) - ((int)pix(1) + 0.5);
               appPtr->offset.P() = jblas::zero_mat(2); // by definition this is our landmark projection
               */
            }

            JFR_DEBUG("returning " << (ret ? "true" : "false"));
            return ret;
         }

         void fillDataObs(const boost::shared_ptr<FeatureSegment> & featPtr, boost::shared_ptr<ObservationAbstract> & obsPtr)
         {
            // extract observed appearance
            app_seg_ptr_t app_src = SPTR_CAST<AppearanceSegment>(featPtr->appearancePtr);
            app_seg_ptr_t app_dst = SPTR_CAST<AppearanceSegment>(obsPtr->observedAppearance);
            app_dst->setHypothesis(app_src->hypothesis());
//            app_src->patch.copyTo(app_dst->patch);
            /*app_src->patch.copy(app_dst->patch, (app_src->patch.width()-app_dst->patch.width())/2,
                  (app_src->patch.height()-app_dst->patch.height())/2, 0, 0,
                  app_dst->patch.width(), app_dst->patch.height());*/
            //app_dst->offset = app_src->offset;

            // create descriptor
            descriptor_ptr_t descPtr(descFactory->createDescriptor());
            obsPtr->landmarkPtr()->setDescriptor(descPtr);
         }

   };

}}

#endif // RAWSEGPROCESSORS_HPP
