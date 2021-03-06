# Copyright 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from measurements import rasterize_and_record
from telemetry import test
from telemetry.core import wpr_modes
from telemetry.page import page_measurement_unittest_base
from telemetry.unittest import options_for_unittests


class RasterizeAndRecordUnitTest(
      page_measurement_unittest_base.PageMeasurementUnitTestBase):
  """Smoke test for rasterize_and_record measurement

     Runs rasterize_and_record measurement on a simple page and verifies that
     all metrics were added to the results. The test is purely functional,
     i.e. it only checks if the metrics are present and non-zero.
  """

  def setUp(self):
    self._options = options_for_unittests.GetCopy()
    self._options.browser_options.wpr_mode = wpr_modes.WPR_OFF
    self._options.raster_record_repeat = 1
    self._options.start_wait_time = 0.0
    self._options.stop_wait_time = 2.0

  @test.Disabled
  def testRasterizeAndRecord(self):
    ps = self.CreatePageSetFromFileInUnittestDataDir('blank.html')
    measurement = rasterize_and_record.RasterizeAndRecord()
    results = self.RunMeasurement(measurement, ps, options=self._options)
    self.assertEquals(0, len(results.failures))

    rasterize_time = results.FindAllPageSpecificValuesNamed('rasterize_time')
    self.assertEquals(len(rasterize_time), 1)
    self.assertGreater(rasterize_time[0].GetRepresentativeNumber(), 0)

    record_time = results.FindAllPageSpecificValuesNamed('record_time')
    self.assertEquals(len(record_time), 1)
    self.assertGreater(record_time[0].GetRepresentativeNumber(), 0)

    rasterized_pixels = results.FindAllPageSpecificValuesNamed(
        'rasterized_pixels')
    self.assertEquals(len(rasterized_pixels), 1)
    self.assertGreater(rasterized_pixels[0].GetRepresentativeNumber(), 0)

    recorded_pixels = results.FindAllPageSpecificValuesNamed('recorded_pixels')
    self.assertEquals(len(recorded_pixels), 1)
    self.assertGreater(recorded_pixels[0].GetRepresentativeNumber(), 0)

  def testCleanUpTrace(self):
    self.TestTracingCleanedUp(rasterize_and_record.RasterizeAndRecord,
                              self._options)
