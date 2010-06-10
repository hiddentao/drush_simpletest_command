== Drush SimpleTest command ===

This adds a 'test' command to Drush, enabling you to run a single SimpleTest test. It has been tested with Drush 3.0-beta1 on Drupal 6.16.

This code is based on the patch by Bevan at http://drupal.org/node/580530. It adds some features such as the ability to toggle detailed test output on and off and more importantly, it captures and displays any exception logs or other critical test failures which the original code failed to do.

To use this simply overwrite drush/commands/simpletest/simpletest.drush.inc with the file provided in this package. 