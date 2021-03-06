// Copyright Contributors to the OpenVDB Project
// SPDX-License-Identifier: MPL-2.0

#include <cppunit/extensions/HelperMacros.h>
#include <openvdb/openvdb.h>
#include <openvdb/math/BBox.h>
#include <openvdb/math/Math.h>
#include <openvdb/tree/Tree.h>
#include <openvdb/tools/GridTransformer.h>
#include <openvdb/tools/Prune.h>

#define ASSERT_DOUBLES_EXACTLY_EQUAL(expected, actual) \
    CPPUNIT_ASSERT_DOUBLES_EQUAL((expected), (actual), /*tolerance=*/0.0);

class TestGridTransformer: public CppUnit::TestCase
{
public:
    CPPUNIT_TEST_SUITE(TestGridTransformer);
    CPPUNIT_TEST(testTransformBoolPoint);
    CPPUNIT_TEST(testTransformFloatPoint);
    CPPUNIT_TEST(testTransformFloatBox);
    CPPUNIT_TEST(testTransformFloatQuadratic);
    CPPUNIT_TEST(testTransformDoubleBox);
    CPPUNIT_TEST(testTransformInt32Box);
    CPPUNIT_TEST(testTransformInt64Box);
    CPPUNIT_TEST(testTransformVec3SPoint);
    CPPUNIT_TEST(testTransformVec3DBox);
    CPPUNIT_TEST(testResampleToMatch);
    CPPUNIT_TEST(testDecomposition);
    CPPUNIT_TEST_SUITE_END();

    void testTransformBoolPoint()
        { transformGrid<openvdb::BoolGrid, openvdb::tools::PointSampler>(); }
    void testTransformFloatPoint()
        { transformGrid<openvdb::FloatGrid, openvdb::tools::PointSampler>(); }
    void testTransformFloatBox()
        { transformGrid<openvdb::FloatGrid, openvdb::tools::BoxSampler>(); }
    void testTransformFloatQuadratic()
        { transformGrid<openvdb::FloatGrid, openvdb::tools::QuadraticSampler>(); }
    void testTransformDoubleBox()
        { transformGrid<openvdb::DoubleGrid, openvdb::tools::BoxSampler>(); }
    void testTransformInt32Box()
        { transformGrid<openvdb::Int32Grid, openvdb::tools::BoxSampler>(); }
    void testTransformInt64Box()
        { transformGrid<openvdb::Int64Grid, openvdb::tools::BoxSampler>(); }
    void testTransformVec3SPoint()
        { transformGrid<openvdb::VectorGrid, openvdb::tools::PointSampler>(); }
    void testTransformVec3DBox()
        { transformGrid<openvdb::Vec3DGrid, openvdb::tools::BoxSampler>(); }

    void testResampleToMatch();
    void testDecomposition();

private:
    template<typename GridType, typename Sampler> void transformGrid();
};


CPPUNIT_TEST_SUITE_REGISTRATION(TestGridTransformer);


////////////////////////////////////////


template<typename GridType, typename Sampler>
void
TestGridTransformer::transformGrid()
{
    using openvdb::Coord;
    using openvdb::CoordBBox;
    using openvdb::Vec3R;

    typedef typename GridType::ValueType ValueT;

    const int radius = Sampler::radius();
    const openvdb::Vec3R zeroVec(0, 0, 0), oneVec(1, 1, 1);
    const ValueT
        zero = openvdb::zeroVal<ValueT>(),
        one = zero + 1,
        two = one + 1,
        background = one;
    const bool transformTiles = true;

    // Create a sparse test grid comprising the eight corners of a 20 x 20 x 20 cube.
    typename GridType::Ptr inGrid = GridType::create(background);
    typename GridType::Accessor inAcc = inGrid->getAccessor();
    inAcc.setValue(Coord( 0,  0,  0),  /*value=*/zero);
    inAcc.setValue(Coord(20,  0,  0),  zero);
    inAcc.setValue(Coord( 0, 20,  0),  zero);
    inAcc.setValue(Coord( 0,  0, 20),  zero);
    inAcc.setValue(Coord(20,  0, 20),  zero);
    inAcc.setValue(Coord( 0, 20, 20),  zero);
    inAcc.setValue(Coord(20, 20,  0),  zero);
    inAcc.setValue(Coord(20, 20, 20),  zero);
    CPPUNIT_ASSERT_EQUAL(openvdb::Index64(8), inGrid->activeVoxelCount());

    // For various combinations of scaling, rotation and translation...
    for (int i = 0; i < 8; ++i) {
        const openvdb::Vec3R
            scale = i & 1 ? openvdb::Vec3R(10, 4, 7.5) : oneVec,
            rotate = (i & 2 ? openvdb::Vec3R(30, 230, -190) : zeroVec) * (M_PI / 180),
            translate = i & 4 ? openvdb::Vec3R(-5, 0, 10) : zeroVec,
            pivot = i & 8 ? openvdb::Vec3R(0.5, 4, -3.3) : zeroVec;
        openvdb::tools::GridTransformer transformer(pivot, scale, rotate, translate);
        transformer.setTransformTiles(transformTiles);

        // Add a tile (either active or inactive) in the interior of the cube.
        const bool tileIsActive = (i % 2);
        inGrid->fill(CoordBBox(Coord(8), Coord(15)), two, tileIsActive);
        if (tileIsActive) {
            CPPUNIT_ASSERT_EQUAL(openvdb::Index64(512 + 8), inGrid->activeVoxelCount());
        } else {
            CPPUNIT_ASSERT_EQUAL(openvdb::Index64(8), inGrid->activeVoxelCount());
        }
        // Verify that a voxel outside the cube has the background value.
        CPPUNIT_ASSERT(openvdb::math::isExactlyEqual(inAcc.getValue(Coord(21, 0, 0)), background));
        CPPUNIT_ASSERT_EQUAL(false, inAcc.isValueOn(Coord(21, 0, 0)));
        // Verify that a voxel inside the cube has value two.
        CPPUNIT_ASSERT(openvdb::math::isExactlyEqual(inAcc.getValue(Coord(12)), two));
        CPPUNIT_ASSERT_EQUAL(tileIsActive, inAcc.isValueOn(Coord(12)));

        // Verify that the bounding box of all active values is 20 x 20 x 20.
        CoordBBox activeVoxelBBox = inGrid->evalActiveVoxelBoundingBox();
        CPPUNIT_ASSERT(!activeVoxelBBox.empty());
        const Coord imin = activeVoxelBBox.min(), imax = activeVoxelBBox.max();
        CPPUNIT_ASSERT_EQUAL(Coord(0),  imin);
        CPPUNIT_ASSERT_EQUAL(Coord(20), imax);

        // Transform the corners of the input grid's bounding box
        // and compute the enclosing bounding box in the output grid.
        const openvdb::Mat4R xform = transformer.getTransform();
        const Vec3R
            inRMin(imin.x(), imin.y(), imin.z()),
            inRMax(imax.x(), imax.y(), imax.z());
        Vec3R outRMin, outRMax;
        outRMin = outRMax = inRMin * xform;
        for (int j = 0; j < 8; ++j) {
            Vec3R corner(
                j & 1 ? inRMax.x() : inRMin.x(),
                j & 2 ? inRMax.y() : inRMin.y(),
                j & 4 ? inRMax.z() : inRMin.z());
            outRMin = openvdb::math::minComponent(outRMin, corner * xform);
            outRMax = openvdb::math::maxComponent(outRMax, corner * xform);
        }

        CoordBBox bbox(
            Coord(openvdb::tools::local_util::floorVec3(outRMin) - radius),
            Coord(openvdb::tools::local_util::ceilVec3(outRMax)  + radius));

        // Transform the test grid.
        typename GridType::Ptr outGrid = GridType::create(background);
        transformer.transformGrid<Sampler>(*inGrid, *outGrid);
        openvdb::tools::prune(outGrid->tree());

        // Verify that the bounding box of the transformed grid
        // matches the transformed bounding box of the original grid.

        activeVoxelBBox = outGrid->evalActiveVoxelBoundingBox();
        CPPUNIT_ASSERT(!activeVoxelBBox.empty());
        const openvdb::Vec3i
            omin = activeVoxelBBox.min().asVec3i(),
            omax = activeVoxelBBox.max().asVec3i();
        const int bboxTolerance = 1; // allow for rounding
#if 0
        if (!omin.eq(bbox.min().asVec3i(), bboxTolerance) ||
            !omax.eq(bbox.max().asVec3i(), bboxTolerance))
        {
            std::cerr << "\nS = " << scale << ", R = " << rotate
                << ", T = " << translate << ", P = " << pivot << "\n"
                << xform.transpose() << "\n" << "computed bbox = " << bbox
                << "\nactual bbox   = " << omin << " -> " << omax << "\n";
        }
#endif
        CPPUNIT_ASSERT(omin.eq(bbox.min().asVec3i(), bboxTolerance));
        CPPUNIT_ASSERT(omax.eq(bbox.max().asVec3i(), bboxTolerance));

        // Verify that (a voxel in) the interior of the cube was
        // transformed correctly.
        const Coord center = Coord::round(Vec3R(12) * xform);
        const typename GridType::TreeType& outTree = outGrid->tree();
        CPPUNIT_ASSERT(openvdb::math::isExactlyEqual(transformTiles ? two : background,
            outTree.getValue(center)));
        if (transformTiles && tileIsActive) CPPUNIT_ASSERT(outTree.isValueOn(center));
        else CPPUNIT_ASSERT(!outTree.isValueOn(center));
    }
}


////////////////////////////////////////


void
TestGridTransformer::testResampleToMatch()
{
    using namespace openvdb;

    // Create an input grid with an identity transform.
    FloatGrid inGrid;
    // Populate it with a 20 x 20 x 20 cube.
    inGrid.fill(CoordBBox(Coord(5), Coord(24)), /*value=*/1.0);
    CPPUNIT_ASSERT_EQUAL(8000, int(inGrid.activeVoxelCount()));
    CPPUNIT_ASSERT(inGrid.tree().activeTileCount() > 0);

    {//test identity transform
        FloatGrid outGrid;
        CPPUNIT_ASSERT(outGrid.transform() == inGrid.transform());
        // Resample the input grid into the output grid using point sampling.
        tools::resampleToMatch<tools::PointSampler>(inGrid, outGrid);
        CPPUNIT_ASSERT_EQUAL(int(inGrid.activeVoxelCount()), int(outGrid.activeVoxelCount()));
        for (openvdb::FloatTree::ValueOnCIter iter = inGrid.tree().cbeginValueOn(); iter; ++iter) {
            ASSERT_DOUBLES_EXACTLY_EQUAL(*iter,outGrid.tree().getValue(iter.getCoord()));
        }
        // The output grid's transform should not have changed.
        CPPUNIT_ASSERT(outGrid.transform() == inGrid.transform());
    }

    {//test nontrivial transform
        // Create an output grid with a different transform.
        math::Transform::Ptr xform = math::Transform::createLinearTransform();
        xform->preScale(Vec3d(0.5, 0.5, 1.0));
        FloatGrid outGrid;
        outGrid.setTransform(xform);
        CPPUNIT_ASSERT(outGrid.transform() != inGrid.transform());

        // Resample the input grid into the output grid using point sampling.
        tools::resampleToMatch<tools::PointSampler>(inGrid, outGrid);

        // The output grid's transform should not have changed.
        CPPUNIT_ASSERT_EQUAL(*xform, outGrid.transform());

        // The output grid should have double the resolution of the input grid
        // in x and y and the same resolution in z.
        CPPUNIT_ASSERT_EQUAL(32000, int(outGrid.activeVoxelCount()));
        CPPUNIT_ASSERT_EQUAL(Coord(40, 40, 20), outGrid.evalActiveVoxelDim());
        CPPUNIT_ASSERT_EQUAL(CoordBBox(Coord(9, 9, 5), Coord(48, 48, 24)),
            outGrid.evalActiveVoxelBoundingBox());
        for (auto it = outGrid.tree().cbeginValueOn(); it; ++it) {
            CPPUNIT_ASSERT_DOUBLES_EQUAL(1.0, *it, 1.0e-6);
        }
    }
}


////////////////////////////////////////


void
TestGridTransformer::testDecomposition()
{
    using namespace openvdb;
    using tools::local_util::decompose;

    {
        Vec3d s, r, t;
        auto m = Mat4d::identity();
        CPPUNIT_ASSERT(decompose(m, s, r, t));
        m(1, 3) = 1.0; // add a perspective component
        // Verify that decomposition fails for perspective transforms.
        CPPUNIT_ASSERT(!decompose(m, s, r, t));
    }

    const auto rad = [](double deg) { return deg * M_PI / 180.0; };

    const Vec3d ix(1, 0, 0), iy(0, 1, 0), iz(0, 0, 1);

    const auto translation = { Vec3d(0), Vec3d(100, 0, -100), Vec3d(-50, 100, 250) };
    const auto scale = { 1.0, 0.25, -0.25, -1.0, 10.0, -10.0 };
    const auto angle = { rad(0.0), rad(45.0), rad(90.0), rad(180.0),
        rad(225.0), rad(270.0), rad(315.0), rad(360.0) };

    for (const auto& t: translation) {

        for (const double sx: scale) {
            for (const double sy: scale) {
                for (const double sz: scale) {
                    const Vec3d s(sx, sy, sz);

                    for (const double rx: angle) {
                        for (const double ry: angle) {
                            for (const double rz: angle) {

                                Mat4d m =
                                    math::rotation<Mat4d>(iz, rz) *
                                    math::rotation<Mat4d>(iy, ry) *
                                    math::rotation<Mat4d>(ix, rx) *
                                    math::scale<Mat4d>(s);
                                m.setTranslation(t);

                                Vec3d outS(0), outR(0), outT(0);
                                if (decompose(m, outS, outR, outT)) {
                                    // If decomposition succeeds, verify that it produces
                                    // the same matrix.  (Most decompositions fail to find
                                    // a unique solution, though.)
                                    Mat4d outM =
                                        math::rotation<Mat4d>(iz, outR.z()) *
                                        math::rotation<Mat4d>(iy, outR.y()) *
                                        math::rotation<Mat4d>(ix, outR.x()) *
                                        math::scale<Mat4d>(outS);
                                    outM.setTranslation(outT);
                                    CPPUNIT_ASSERT(outM.eq(m));
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

