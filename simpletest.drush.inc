<?php
// $Id: simpletest.drush.inc,v 1.6 2010/01/20 16:23:38 weitzman Exp $

// Load Drush simpletest commands
require_once('drushsimpletest.inc');

/**
 * Implementation of hook_drush_help().
 */
function simpletest_drush_help($section) {
  switch ($section) {
    case 'drush:test':
      return dt("Run a specified test and outputs the results on the command line, and optionally outputs XML and send an email to specified recipients.");
    case 'drush:test-list':
      return dt("List all available tests.");
    case 'drush:test clean':
      return dt("Clean leftover tables and file directories from prior test runs.");
  }
}

/**
 * Implementation of hook_drush_command().
 */
function simpletest_drush_command() {
  $items['test'] = array(
      'description' => 'Run a specified test and outputs the results on the command line, and optionally outputs XML and send an email to specified recipients.',
      'examples' => array(
        "drush test" => "Run all available tests and outputs the results on the command line.",
        "drush test BlockTestCase" => "Run BlockTestCase and outputs the results on the command line.",
        "drush test --quiet BlockTestCase" => "Run BlockTestCase quietly, i.e. without outputting results to the command line (test failures will still get output).",
        "drush test --quiet --mail=admin@test.com BlockTestCase" => "Run BlockTestCase quietly and email the results to admin@test.com",
        "drush test --xml=/var/output BlockTestCase" => "Run BlockTestCase and output results on the command line and also into an XML file inside the /var/output folder",
        ),
      'arguments' => array(
        'test_class' => 'The test case you want to run.',
        ),
      'options' => array(
        '--quiet' => 'Do not output to the command-line (test failures will still get output).',
        '--color' => 'Use color highlighting for results output.',
        '--exit-on-fail' => 'Exit to console with an error code if the test fails.',
        '--mail=<recipients>' => 'Mail the recipients the output. Separate multiple recipients with a comma.',
        '--xml=<path>' => 'The directory in which to output XML test results (absolute path required).',
        ),
      'drupal dependencies' => array('simpletest'),
      'core' => array('6','7'),
      'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_FULL,
      );
  $items['test-list'] = array(
      'description' => 'List all tests',
      'drupal dependencies' => array('simpletest'),
      'core' => array('6','7'),
      );
  $items['test-clean'] = array(
      'description' => 'Delete leftover tables and files from prior test runs.',
      'drupal dependencies' => array('simpletest'),
      'core' => array('6','7'),
      );
  $items['test-drush'] = array(
      'description' => 'Run drush-specific tests',
      'bootstrap' => DRUSH_BOOTSTRAP_DRUSH,
      );
  return $items;
}

// A Drush command callback.
function drush_simpletest_test($test_class=NULL) {
  $processors = array();

  // Stdout processor
  $quiet = drush_get_option('quiet');
  if(is_null($quiet))
    $processors[] = new DrushSimpleTestPrintProcessor(!is_null(drush_get_option('color')));

  // Mail processor
  $recipients = drush_get_option('mail');
  if(!is_null($recipients))
    $processors[] = new DrushSimpleTestMailProcessor(explode(",", $recipients));

  // XML processor
  $xml = drush_get_option('xml');
  if(!is_null($xml))
    $processors[] = new DrushSimpleTestXMLProcessor($xml);

  $test = new DrushSimpleTest($processors);

  // exit with error code if test fails?
  $exit_on_fail = !is_null(drush_get_option('exit-on-fail'));
  
  // Actually run the tests
  $error = FALSE;
  try {
    if($test_class) {
      $test->test_class($test_class);
    } else {
      $test->test_all_classes();
    }
  } catch(DrushSimpleTestException $e) {
  	$error = $e->getMessage();
  	drush_log($e->getMessage(), 'warning', $e->getDetail());
  } catch(Exception $e) {
    $error = $e->getMessage();
    drush_set_error($e->getMessage(), $e->getTraceAsString());
  }

  // Process the results
  $test->process_results();
  
  // error?
  if ($error && $exit_on_fail)
	    exit(-1);
}

// A Drush command callback.
function drush_simpletest_test_list() {
  $test = new DrushSimpleTest();

  drush_print("\nAvailable test groups & classes");
  drush_print("-------------------------------");
  $current_group = '';
  foreach ($test->get_test_classes() as $class => $details) {
    if (class_exists($class) && method_exists($class, 'getInfo')) {
      $info = call_user_func(array($class, 'getInfo'));
      if ($info['group'] != $current_group) {
        $current_group = $info['group'];
        drush_print('[' . $current_group . ']');
      }
      drush_print("\t" . $class . ' - ' . $info['name']);
    }
  }
}

// A drush command callback.
function drush_simpletest_test_clean() {
  $test = new DrushSimpleTest();
  $test->clean();
  drush_print("Simpletest environment successfully cleaned.");
}

/**
 * Simple drush self-test procedure
 *
 * This only tests self-execution for now.
 *
 * XXX: this needs to be adapted to a testing framework, see:
 *
 * http://drupal.org/node/483940
 */
function drush_simpletest_test_drush() {
  drush_log(dt("Invoking %drush help in a subprocess", array('%drush' => DRUSH_COMMAND)));
  drush_backend_invoke('help', array(), 'GET', FALSE);
}
