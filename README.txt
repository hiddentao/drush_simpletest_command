== Drush SimpleTest command ===

This adds a 'test' command to Drush, enabling you to run a single SimpleTest test. It has been tested with Drush 6.x-3.3 on Drupal 6.16.

This code is based on the patch by Bevan at http://drupal.org/node/580530. It adds some features such as the ability to toggle detailed test output on and off and more importantly, it captures and displays any exception logs or other critical test failures which the original code failed to do.

It also adds the ability to email a list of recipients the rest results as well as output the results in XML format.

To use this simply overwrite drush/commands/simpletest/simpletest.drush.inc with the file provided in this package. 