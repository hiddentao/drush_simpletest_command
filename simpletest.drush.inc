<?php
// $Id: simpletest.drush.inc,v 1.6 2010/01/20 16:23:38 weitzman Exp $

/**
 * @file Drush simpletest commands
 */

/**
 * Implementation of hook_drush_help().
 */
function simpletest_drush_help($section) {
  switch ($section) {
    case 'drush:test':
      return dt("Run a specified test and output the results on the command line.");
    case 'drush:test-mail':
      return dt("Run tests and email the results. See the docs for run-tests.sh to understand --extra.");
    case 'drush:test clean':
      return dt("Clean leftover tables and file directories from prior test runs.");
  }
}

/**
 * Implementation of hook_drush_command().
 */
function simpletest_drush_command() {
  $items['test'] = array(
      'description' => 'Run a specified test and outputs the results on the command line.',
      'examples' => array(
        "drush test BlockTestCase" => "Run BlockTestCase and output results on the command line.",
        "drush test" => "Run all test cases and output results on the command line.",
        ),
      'arguments' => array(
        'test_class' => 'The test case you want to run.',
        ),
      'options' => array(
        '--detail' => 'Show detailed test output.',
        '--color' => 'Use color highlighting for results output.',
        '--error-on-fail' => 'Exit to console with an error code if the test fails.',
        '--mail' => 'Mail the recipients the output',
        '--xml' => 'The directory to output XML test results',
        ),
      'drupal dependencies' => array('simpletest', 'drushsimpletest'),
      'core' => array('6','7'),
      'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_FULL,
      );
  $items['test-list'] = array(
      'description' => 'List all tests',
      'drupal dependencies' => array('simpletest', 'drushsimpletest'),
      'core' => array('6','7'),
      );
  $items['test-clean'] = array(
      'description' => 'Delete leftover tables and files from prior test runs.',
      'drupal dependencies' => array('simpletest', 'drushsimpletest'),
      'core' => array('6','7'),
      );
  $items['test-drush'] = array(
      'description' => 'Run drush-specific tests',
      'bootstrap' => DRUSH_BOOTSTRAP_DRUSH,
      );
  return $items;
}

// A drush command callback.
function drush_simpletest_test_clean() {
  $test = new DrushSimpleTest();
  $test->clean();
  drush_print("Simpletest environment successfully cleaned.");
}

// A Drush command callback.
function drush_simpletest_test($test_class=NULL) {

  $verbose = !is_null(drush_get_option('detail'));
  $color = !is_null(drush_get_option('color'));
  $error_on_fail = !is_null(drush_get_option('error-on-fail'));

  $processors = array();
  $processors[] = new DrushSimpleTestPrintProcessor($verbose, $color);
  if(!is_null(drush_get_option('recipients'))) {
    $processors[] = new DrushSimpleTestMailProcessor(explode(",", drush_get_option('recipients')));
  }
  $test = new DrushSimpleTest($processors);

  if($test_class) {
    try {
      $test->test_class($test_class);
      $processor->process_results;
    } catch(DrushSimpleTestException $e) {
      drush_set_error($e->getMessage(), $e->getDetail());
    }
  } else {
    $test->test_all_classes($error_on_fail);
  }
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
