#pragma once

#include <boost/math/tools/roots.hpp>

#include "utils/spline_common.h"

namespace __ArcLengthSolvePrivate
{
    //solve the arc length for a single spline segment
    template<template <class, typename> class Spline, class InterpolationType, typename floating_t>
    floating_t solveSegment(const Spline<InterpolationType, floating_t>& spline, size_t segmentIndex, floating_t desiredLength, floating_t maxLength, floating_t aPercent)
    {
        //we can use the lengths we've calculated to formulate a pretty solid guess
        //if desired length is x% of the bLength, then our guess will be x% of the way from aPercent to 1
        floating_t desiredPercent = desiredLength / maxLength;
        floating_t bGuess = aPercent + desiredPercent * (1 - aPercent);

        floating_t bBegin = spline.segmentT(segmentIndex);
        floating_t bEnd = spline.segmentT(segmentIndex + 1);

        auto solveFunction = [&](floating_t bPercent) {
            floating_t value = spline.segmentArcLength(segmentIndex, aPercent, bPercent) - desiredLength;

            floating_t b = bBegin + bPercent * (bEnd - bBegin);

            //the derivative will be the length of the tangent
            auto interpolationResult = spline.getCurvature(b);
            floating_t tangentLength = interpolationResult.tangent.length();

            //the second derivative will be the curvature projected onto the tangent
            interpolationResult.tangent /= tangentLength;
            floating_t secondDerivative = InterpolationType::dotProduct(interpolationResult.tangent, interpolationResult.curvature);

            return std::make_tuple(value, tangentLength, secondDerivative);
        };

        return boost::math::tools::halley_iterate(solveFunction, bGuess, aPercent, floating_t(1), int(std::numeric_limits<floating_t>::digits * 0.5));
    }
}

namespace ArcLength
{
    //compute b such that arcLength(a,b) == desiredLength
    template<template <class, typename> class Spline, class InterpolationType, typename floating_t>
    floating_t solveLength(const Spline<InterpolationType, floating_t>& spline, floating_t a, floating_t desiredLength)
    {
        size_t aIndex = spline.segmentForT(a);
        size_t bIndex = aIndex;

        floating_t aBegin = spline.segmentT(aIndex);
        floating_t aEnd = spline.segmentT(aIndex + 1);
        floating_t aPercent = (a - aBegin) / (aEnd - aBegin);

        floating_t aLength = spline.segmentArcLength(aIndex, aPercent, 1);
        floating_t bLength = aLength;

        //if aLength is less than desiredLength, B will be in a different segment than A, so search though the spline until we find B's segment
        if(aLength < desiredLength)
        {
            aPercent = 0;
            desiredLength -= aLength;

            //scan through the middle segments, stopping when we reach the end of the spline or we reach the segment that contains b
            while(++bIndex < spline.segmentCount())
            {
                bLength = spline.segmentArcLength(bIndex, 0, 1);

                if(bLength < desiredLength)
                {
                    desiredLength -= bLength;
                }
                else
                {
                    break;
                }
            }
        }

        //if bIndex is equal to the segment count, we've hit the end of the spline, so return maxT
        if(bIndex == spline.segmentCount()) {
            return spline.getMaxT();
        }

        //we now know our answer lies somewhere in the segment bIndex
        floating_t bPercent = __ArcLengthSolvePrivate::solveSegment(spline, bIndex, desiredLength, bLength, aPercent);

        floating_t bBegin = spline.segmentT(bIndex);
        floating_t bEnd = spline.segmentT(bIndex + 1);
        return bBegin + bPercent * (bEnd - bBegin);
    }


    //subdivide the spline into pieces such that the arc length of each pieces is equal to desiredLength
    //returns a list of t values marking the boundaries of each piece
    //the first entry is always 0. the final entry is the T value that marks the end of the last cleanly-dividible piece
    //The remainder that could not be divided is the piece between the last entry and maxT
    template<template <class, typename> class Spline, class InterpolationType, typename floating_t>
    std::vector<floating_t> partition(const Spline<InterpolationType, floating_t>& spline, floating_t lengthPerPiece)
    {
        //first, compute total arc length and arc length for each segment
        std::vector<floating_t> segmentLengths(spline.segmentCount());
        floating_t totalArcLength(0);
        for(size_t i = 0; i < spline.segmentCount(); i++)
        {
            floating_t segmentLength = spline.segmentArcLength(i, 0, 1);
            totalArcLength += segmentLength;
            segmentLengths[i] = segmentLength;
        }

        std::vector<floating_t> pieces(size_t(totalArcLength / lengthPerPiece) + 1);

        floating_t segmentRemainder = segmentLengths[0];
        floating_t previousPercent = 0;
        size_t aIndex = 0;

        //for each piece, perform the same algorithm as the "solve" method, but we have much fewer arc lenths to compute
        //because we can reuse work between segments
        for(size_t i = 1; i < pieces.size(); i++)
        {
            size_t bIndex = aIndex;

            floating_t desiredLength = lengthPerPiece;

            //if aLength is less than desiredLength, B will be in a different segment than A, so search though the spline until we find B's segment
            while(segmentRemainder < desiredLength)
            {
                desiredLength -= segmentRemainder;
                segmentRemainder = segmentLengths[++bIndex];
            }

            floating_t aPercent;
            if(aIndex == bIndex)
            {
                aPercent = previousPercent;
            }
            else
            {
                aPercent = 0;
            }

            //we now know our answer lies somewhere in the segment bIndex
            floating_t bPercent = __ArcLengthSolvePrivate::solveSegment(spline, bIndex, desiredLength, segmentRemainder, aPercent);

            floating_t bBegin = spline.segmentT(bIndex);
            floating_t bEnd = spline.segmentT(bIndex + 1);
            pieces[i] = bBegin + bPercent * (bEnd - bBegin);

            //set up the next iteration of the loop
            previousPercent = bPercent;
            segmentRemainder = segmentRemainder - desiredLength;
            aIndex = bIndex;
        }
        return pieces;
    }

    //subdivide the spline into N pieces such that each piece has the same arc length
    //returns a list of N+1 T values, where return[i] is the T value of the beginning of a piece and return[i+1] is the T value of the end of a piece
    //the first element in the returned list is always 0, and the last element is always spline.getMaxT()
    template<template <class, typename> class Spline, class InterpolationType, typename floating_t>
    std::vector<floating_t> partitionN(const Spline<InterpolationType, floating_t>& spline, size_t n)
    {
        //first, compute total arc length and arc length for each segment
        std::vector<floating_t> segmentLengths(spline.segmentCount());
        floating_t totalArcLength(0);
        for(size_t i = 0; i < spline.segmentCount(); i++)
        {
            floating_t segmentLength = spline.segmentArcLength(i, 0, 1);
            totalArcLength += segmentLength;
            segmentLengths[i] = segmentLength;
        }
        const floating_t lengthPerPiece = totalArcLength / n;

        //set up the result vector
        std::vector<floating_t> pieces(n + 1);
        pieces[0] = 0;
        pieces[n] = spline.getMaxT();

        //set up the inter-piece state
        floating_t segmentRemainder = segmentLengths[0];
        floating_t previousPercent = 0;
        size_t aIndex = 0;

        //for each piece, perform the same algorithm as the partition" method
        for(size_t i = 1; i < n; i++)
        {
            size_t bIndex = aIndex;

            floating_t desiredLength = lengthPerPiece;

            //if aLength is less than desiredLength, B will be in a different segment than A, so search though the spline until we find B's segment
            while(segmentRemainder < desiredLength)
            {
                desiredLength -= segmentRemainder;
                segmentRemainder = segmentLengths[++bIndex];
            }

            floating_t aPercent;
            if(aIndex == bIndex)
            {
                aPercent = previousPercent;
            }
            else
            {
                aPercent = 0;
            }

            //we now know our answer lies somewhere in the segment bIndex
            floating_t bPercent = __ArcLengthSolvePrivate::solveSegment(spline, bIndex, desiredLength, segmentRemainder, aPercent);

            floating_t bBegin = spline.segmentT(bIndex);
            floating_t bEnd = spline.segmentT(bIndex + 1);

            pieces[i] = bBegin + bPercent * (bEnd - bBegin);

            //set up the next iteration of the loop
            previousPercent = bPercent;
            segmentRemainder = segmentRemainder - desiredLength;
            aIndex = bIndex;
        }
        return pieces;
    }
}
