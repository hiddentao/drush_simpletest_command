<?php
// $Id: simpletest.drush.inc,v 1.6 2010/01/20 16:23:38 weitzman Exp $

/**
 * Modified version of simpletest.drush.inc.
 *
 * Original patch applied from http://drupal.org/node/580530 (http://drupal.org/user/49989)
 */

/**
 * @file Drush simpletest commands
 */

// SimpleTest console output colors
define('_SIMPLETEST_SCRIPT_COLOR_PASS', 32); // Green.
define('_SIMPLETEST_SCRIPT_COLOR_FAIL', 31); // Red.
define('_SIMPLETEST_SCRIPT_COLOR_EXCEPTION', 33); // Brown.

define('_SIMPLETEST_TIMER_NAME','simpletest_timer');


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
      'callback' => 'drush_simpletest_test_one',
      'description' => 'Run a specified test and outputs the results on the command line.',
      'examples' => array(
        "drush test BlockTestCase" => "Run BlockTestCase and output results on the command line.",
        ),
      'arguments' => array(
        'test_class' => 'The test case you want to run.',
        ),
      'options' => array(
        '--detail' => 'Show detailed test output.',
        '--color' => 'Use color highlighting for results output.',
        '--error-on-fail' => 'Exit to console with an error code if the test fails.',
        ),
      'drupal dependencies' => array('simpletest'),
      'core' => array('6','7'),
      'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_FULL,
      );
  $items['test-mail'] = array(
      'description' => 'Run all tests and mail the results to your team.',
      'examples' => array(
        "drush test-mail me@example.com --extra=\"--class BlockTestCase\"" => "Run block tests & email to me.",
        "drush test-mail me@example.com --extra=\"--all\"" => "Run all tests & email to me.",
        ),
      'arguments' => array(
        'recipients' => 'A comma delimited list of email addrresses.',
        ),
      'options' => array(
        '--extra' => 'Additional parameters to be passed to run-tests.sh',
        '--php' => 'Full path to your PHP interpreter. Usually not needed.'
        ),
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

// A drush command callback.
function drush_simpletest_test_clean() {
  simpletest_clean_environment();
  drush_print("Simpletest environment successfully cleaned.");
}

// A Drush command callback.
function drush_simpletest_test_one($test_class='') {

  global $verbose, $color;
  $verbose = is_null(drush_get_option('detail')) ? FALSE : TRUE;
  $color = is_null(drush_get_option('color')) ? FALSE : TRUE;
  $error_on_fail = is_null(drush_get_option('error-on-fail')) ? FALSE : TRUE;

  // call this method rather than simpletest_test_get_all() in order to bypass internal cache
  $all_test_classes = simpletest_test_get_all_classes();

  // Check that the test class parameter has been set.
  if (empty($test_class)) {
    drush_print("\nAvailable test groups & classes");
    drush_print("-------------------------------");
    $current_group = '';
    foreach ($all_test_classes as $class => $details) {
      if (class_exists($class) && method_exists($class, 'getInfo')) {
        $info = call_user_func(array($class, 'getInfo'));
        if ($info['group'] != $current_group) {
          $current_group = $info['group'];
          drush_print('[' . $current_group . ']');
        }
        drush_print("\t" . $class . ' - ' . $info['name']);
      }
    }
    return;
  }

  // Check the parameter is valid.
  if (!isset($all_test_classes[$test_class]) && class_exists($test_class)) {
    drush_set_error('simpletest test case does not exist', t("Test case !case not found.  Perhaps you spelt it wrong or need to enable the module?\nUse the drush test command with no arguments to see a list of available test cases.  E.g.\n\n\tdrush test\n", array('!case' => $test_class)));
    return;
  }

  // Run tests.  We need an ID to get be able to retrieve errors.
  db_query('INSERT INTO {simpletest_test_id} (test_id) VALUES (default)');
  $id = db_last_insert_id('simpletest_test_id', 'test_id');
  $test = new $test_class($id);
  _simpletest_script_reporter_init($test_class);
  $test->run();

  // pass or fail?
  $status = ((isset($test->results['#fail']) && $test->results['#fail'] > 0)
      || (isset($test->results['#exception']) && $test->results['#exception'] > 0) ? 'fail' : 'pass');

  // show verbose if test failed
  if ('fail' == $status)
    $verbose = TRUE;

  // grab any fatal errors that were logged before we clear the db
  list($last_prefix, $last_test_class) = simpletest_last_test_get($id);
  simpletest_log_read($id, $last_prefix, $last_test_class);

  // Display results before database is cleared.
  _simpletest_script_reporter_display_results($id);

  // Cleanup our test results.
  simpletest_clean_results_table($id);

  if ('fail' == $status) {
    drush_print("Test $test_class FAILED!");
    if ($error_on_fail)
      exit(-1);
  }
  else
    drush_print("Test $test_class PASSED.");
}


// A Drush command callback.
function drush_simpletest_test_mail($recipients) {
  $run_tests = './scripts/run-tests.sh';
  if (!file_exists($run_tests)) {
    return drush_set_error('DRUSH_SIMPLETEST_RUNTESTS_SH', dt('You must copy or symlink run-tests.sh into your /scripts directory beneath Drupal root.'));
  }

  $extra = drush_get_option('extra');
  $url = escapeshellarg(url('', array('absolute' => TRUE)));
  $exec = "$run_tests --php '" . DRUSH_COMMAND . '\' --url ' . $url . " $extra";
  drush_shell_exec($exec);
  $output =  implode("\n", drush_shell_exec_output());
  $subject = 'Simpletest results - ' . drush_simpletest_format_results($output);
  mail($recipients, $subject, $output);
}

// Based on pifr_review_format_results(). Not working yet. Help wanted.
function drush_simpletest_format_results($output) {
  if (preg_match_all('/^(.*?) (\d+) (?:pass|passes), (\d+) fails?, and (\d+) exceptions?$/m', $output, $matches, PREG_SET_ORDER)) {
    $results = array('pass' => 0, 'fail' => 0, 'exception' => 0);
    foreach ($matches as $match) {
      // Add to results count array.
      $results['pass'] += $match[2];
      $results['fail'] += $match[3];
      $results['exception'] += $match[4];
    }
    return $results['pass']  . ' pass, ' . $results['fail'] . ' failures, ' . $results['exceptions'] . '  exceptions';
  }
  else {
    return dt('Unknown.');
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


// ------------------------------------------------------ //
// Copied from SimpleTests's run-tests.sh
// ------------------------------------------------------ //

/**
 * Initialize the reporter.
 */
function _simpletest_script_reporter_init($test_class) {
  drush_print("Test to be run:\n\t" . $test_class . "\n");
  timer_start(_SIMPLETEST_TIMER_NAME);

  drush_print('Test summary:');
  drush_print('---------------------------');
}


/**
 * Display test results.
 */
function _simpletest_script_reporter_display_results($test_id) {

  global $verbose;

  $end = timer_stop(_SIMPLETEST_TIMER_NAME);
  drush_print('Test run duration: ' . format_interval($end['time'] / 1000) . "\n");

  if ($verbose) {
    // Report results.
    drush_print('Detailed results:');
    drush_print('---------------------------');

    $results_map = array(
        'pass' => 'Pass',
        'fail' => 'Fail',
        'exception' => 'Exception'
        );

    $results = db_query('SELECT * FROM {simpletest} WHERE test_id = %d ORDER BY test_class, message_id', $test_id);

    $test_class = '';
    while ($result = db_fetch_object($results)) {
      if (isset($results_map[$result->status])) {
        if ($result->test_class != $test_class) {
          // Display test class every time results are for new test class.
          $test_class = $result->test_class;
        }

        _simpletest_script_format_result($result, $results_map);
      }
    }
  }
}


/**
 * Format the result so that it fits within the default 80 character
 * terminal size.
 *
 * @param $result The result object to format.
 */
function _simpletest_script_format_result($result, $results_map) {
  $summary = sprintf("%-10.10s %-30.30s %-5.5s\n",
      $results_map[$result->status], basename($result->file), $result->line);

  _simpletest_script_print($summary, _simpletest_script_color_code($result->status));

  $lines = explode("\n", wordwrap(trim(strip_tags($result->message)), 76));
  foreach ($lines as $line) {
    drush_print('    '.$line);
  }
}

/**
 * Print error message prefixed with "  ERROR: " and displayed in fail color
 * if color output is enabled.
 *
 * @param $message The message to print.
 */
function _simpletest_script_print_error($message) {
  _simpletest_script_print("  ERROR: $message", _SIMPLETEST_SCRIPT_COLOR_FAIL);
}

/**
 * Print a message to the console, if color is enabled then the specified
 * color code will be used.
 *
 * @param $message The message to print.
 * @param $color_code The color code to use for coloring.
 */
function _simpletest_script_print($message, $color_code) {
  global $color;
  if ($color) {
    drush_print("\033[" . $color_code . "m" . $message . "\033[0m");
  }
  else {
    drush_print($message);
  }
}


/**
 * Get the color code associated with the specified status.
 *
 * @param $status The status string to get code for.
 * @return Color code.
 */
function _simpletest_script_color_code($status) {
  switch ($status) {
    case 'pass':
      return _SIMPLETEST_SCRIPT_COLOR_PASS;
    case 'fail':
      return _SIMPLETEST_SCRIPT_COLOR_FAIL;
    case 'exception':
      return _SIMPLETEST_SCRIPT_COLOR_EXCEPTION;
  }
  return 0; // Default formatting.
}

