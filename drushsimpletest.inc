<?php
/**
 * Drush simpletest class. Does all the heavy lifting for testing and uses 0 or
 * more processors to return the results.
 */
class DrushSimpleTest {
  const SIMPLETEST_TIMER_NAME = 'simpletest_timer';

  /** @var array contains all the test classes */
  private $test_classes;

  /** @var array contains the output processors */
  private $processors;

  /**
   * Constructor
   *
   * @param array $processors The output processors
   */
  public function __construct(array $processors=array()) {
    $this->test_classes = simpletest_test_get_all_classes();
    $this->processors = $processors;

    // Make sure that at shutdown we process the results
    register_shutdown_function(array($this, "process_results"));
  }

  /**
   * Returns all the test classes. Each key is the class name.
   *
   * @return array
   */
  public function get_test_classes() {
    return $this->test_classes;
  }

  /**
   * Returns whether the test class is a valid test class
   *
   * @return bool
   */
  public function is_valid_test_class($test_class) {
    return array_key_exists($test_class, $this->test_classes) && class_exists($test_class);
  }

  /**
   * Runs all test classes.
   *
   * @todo Clean up & document exception handlign
   */
  public function test_all_classes($stop_on_error) {
    $error = FALSE;

    // Run all tests
    foreach($this->test_classes as $class => $details) {
      try {
        $this->test_class($class);
      } catch(Exception $e) {
        $error = TRUE;
        if($stop_on_error) {
          throw $e;
        }
      }
    }

    if($error) {
      throw new DrushSimpleTestException("A Drupal test failed");
    }
  }

  /**
   * Processes the results.
   */
  public function process_results() {
    foreach($this->processors as $processor) {
      $processor->process_results();
    }
  }

  /**
   * Tests a single test class.
   */
  public function test_class($test_class) {
    if(!$this->is_valid_test_class($test_class)) {
      throw new DrushSimpleTestException("Test class '$test_class' not found",
          t("Test case !case not found.  Perhaps you spelt it wrong or need to enable the module?", array('!case' => $test_class)));
    }

    // Run tests.  We need an ID to get be able to retrieve errors.
    db_query('INSERT INTO {simpletest_test_id} (test_id) VALUES (default)');
    $id = db_last_insert_id('simpletest_test_id', 'test_id');
    $test = new $test_class($id);

    // Run the test and time it
    timer_start(self::SIMPLETEST_TIMER_NAME);
    $test->run();
    $end = timer_stop(self::SIMPLETEST_TIMER_NAME);

    // pass or fail?
    $failed = (isset($test->results['#fail']) && $test->results['#fail'] > 0)
      || (isset($test->results['#exception']) && $test->results['#exception'] > 0);

    // grab any fatal errors that were logged before we clear the db
    list($last_prefix, $last_test_class) = simpletest_last_test_get($id);
    simpletest_log_read($id, $last_prefix, $last_test_class);

    // Process the output
    foreach($this->processors as $processor) {
      $processor->add_suite($test_class, $end['time']/1000);
    }

    $results = db_query('SELECT * FROM {simpletest} WHERE test_id = %d ORDER BY test_class, message_id', $id);
    while($result = db_fetch_object($results)) {
      foreach($this->processors as $processor) {
        $processor->add_result($result);
      }
    }

    // Cleanup our test results.
    simpletest_clean_results_table($id);

    // Throw an exception if it failed
    if ($failed) {
      throw new DrushSimpleTestException("Test '$test_class' failed", $result);
    }
  }

  public function clean() {
    simpletest_clean_environment();
  }
}

/**
 * Exception class with a detail message
 */
class DrushSimpleTestException extends Exception {
  private $detail;

  public function __construct($message, $detail=NULL) {
    parent::__construct($message);
    $this->detail = $detail;
  }

  public function getDetail() {
    return $this->detail;
  }
}

/**
 * An abstract class which formats the results obtained by tests.
 */
abstract class DrushSimpleTestProcessor {
  /**
   * array($class => array('time' => float, 'functions' => array($function_name => array('message1', ...))))
   * @var array An array that stores the test results
   */
  protected $tests;

  /**
   * Constructor
   */
  protected function __construct() {
    $this->tests = array();
  }

  /**
   * Add a suite which is basically a test class.
   */
  public function add_suite($class, $time=0) {
    $this->tests[$class] = array('time' => $time, 'functions' => array());
  }

  /**
   * Function to process a single result. Is called by #add_suite.
   *
   * @param $result A single result object as retrieved from the simpletest
   * database.
   */
  public function add_result($result, $time=0) {
    list($class, $name) = explode('->', $result->function, 2);

    if(!isset($this->tests[$class])) {
      $this->add_suite($class);
    }

    $functions = $this->tests[$class]['functions'];
    if(!isset($functions[$name])) {
      $this->tests[$class]['functions'][$name] = array();
    }

    $f_res['time'] = $time;
    $f_res['status'] = $result->status;
    $f_res['message_group'] = $result->message_group;
    $f_res['message'] = $result->message;
    $f_res['file'] = $result->file;

    $this->tests[$class]['functions'][$name][] = $f_res;
  }

  /**
   * Function to be called to process the results. For example, output to stdout
   */
  public abstract function process_results();
}

/**
 * Formats the output to JUnit compatible XML.
 */
class DrushSimpleTestXMLProcessor extends DrushSimpleTestProcessor {
  private $xml_dir;
  private $xml_files;

  public function __construct($xml_dir) {
    $this->xml_dir = $xml_dir;
    $this->xml_files = array();
  }

  public function process_results() {
    // Ensure the directory exists
    if(!is_dir($this->xml_dir)) {
      mkdir($this->xml_dir, 0755, TRUE);
    }

    foreach($this->tests as $class => $results) {
      // Create the basic document
      $doc = new DomDocument('1.0');
      $doc->formatOutput = true;
      $root = $doc->createElement('testsuite');
      $root = $doc->appendChild($root);
      $root->setAttribute('time', $results['time']);

      // Store each result
      foreach($results['functions'] as $function => $messages) {
        $case = $doc->createElement('testcase');
        $case->setAttribute('classname', $class);
        $case->setAttribute('name', $function);
        //$case->setAttribute('time', $time);
        $case = $root->appendChild($case);

        // Store each message if they're failures
        foreach($messages as $message) {
          if ($message['status'] == 'fail' || $message['status'] == 'exception') {
            $fail = $doc->createElement('failure');
            $fail->setAttribute('type', $message['status']);
            $fail->setAttribute('message', $message['message_group']);
            $text = $doc->createTextNode($message['message']);
            $fail->appendChild($text);
            $case->appendChild($fail);
          }
        }

        // Save the file
        $file = $this->xml_dir . '/' . $class . '.xml';
        $content = $doc->saveXML();
        file_put_contents($file, $content);
      }
    }
  }
}

/**
 * Formats the output and prints to stdout using drush_print.
 */
class DrushSimpleTestPrintProcessor extends DrushSimpleTestProcessor {
  private $color;

  public function __construct($color=TRUE) {
    $this->results = array();
    $this->color = $color;
  }

  /**
   * Format the result so that it fits within the default 80 character
   * terminal size.
   */
  public function process_results() {
    // FIXME Implement me
    #foreach($this->results as $result) {
    #  $summary = sprintf("%-10.10s %-30.30s %-5.5s\n",
    #      ucfirst($result->status), basename($result->file), $result->line);

    #  $this->print_message($summary, self::color_code($result->status));

    #  $lines = explode("\n", wordwrap(trim(strip_tags($result->message)), 76));
    #  foreach ($lines as $line) {
    #    drush_print('    '.$line);
    #  }
    #}
  }

  /**
   * Get the color code associated with the specified status.
   *
   * @param $status The status string to get code for.
   * @return Color code.
   */
  private static function color_code($status) {
    switch ($status) {
      case 'pass':
        return 32; // Green
      case 'fail':
        return 31; // Red
      case 'exception':
        return 33; // Brown
      default:
        return 0;
    }
  }

  /**
   * Print a message to the console, if color is enabled then the specified
   * color code will be used.
   *
   * @param $message The message to print.
   * @param $color_code The color code to use for coloring.
   */
  private function print_message($message, $color_code) {
    if ($this->color) {
      drush_print("\033[" . $color_code . "m" . $message . "\033[0m");
    } else {
      drush_print($message);
    }
  }
}

/**
 * A simple processor which mails the results
 */
class DrushSimpleTestMailProcessor extends DrushSimpleTestProcessor {
  private $recipients;
  private $results;
  private $statuses;

  public function __construct(array $recipients) {
    $this->recipients = $recipients;
    $this->results = array();
    $this->statuses = array();
  }

  #public function add_result($result, $time=0) {
  #  $this->results[] = $result;

  #  if(!isset($this->statuses[$result->status])) {
  #    $this->statuses[$result->status] = 0;
  #  } else {
  #    $this->statuses[$result->status]++;
  #  }
  #}

  /**
   * Processes the results by mailing
   */
  public function process_results() {
  #  // FIXME implement me
  #  if($this->results) {
  #    // Build subject
  #    $statuses = array();
  #    foreach($this->statuses as $status => $count) {
  #      $statuses[] = $count . " " . $status;
  #    }
  #    $subject = "Test results - " . implode(", ", $statuses);

  #    $output = "Test results\n\n";
  #    foreach($results as $result) {
  #      $output .= "Class: " . $result->class . "\n";
  #      $output .= wordwrap(trim(strip_tags($result->message)), 79);
  #    }

  #    // Mail the results
  #    mail(implode(",", $recipients), $subject, $output);
  #  }
  }
}

// Change errors into exceptions
set_error_handler("error_to_exception", E_ERROR );
function error_to_exception($errno, $errstr, $errfile, $errline, $errcontext) {
  throw new ErrorException($errno, $errstr, $errfile, $errline, $errcontext);
}
