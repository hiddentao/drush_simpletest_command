<?php
/**
 * Drush simpletest class. Does all the heavy lifting for testing and uses 0 or
 * more processors to return the results.
 */
class DrushSimpleTest {
  const SIMPLETEST_TIMER_NAME = 'simpletest_timer';

  private $test_classes;
  private $processor;

  public function __construct(array $processors=array()) {
    $this->test_classes = simpletest_test_get_all_classes();
    $this->processors = $processors;
  }

  public function get_test_classes() {
    return $this->test_classes;
  }

  public function test_all_classes(boolean $stop_on_error) {
    // Run all tests
    foreach($this->test_classes as $class => $details) {
      try {
        $this->test_class($class);
      } catch(DrushSimpleTestException $e) {
        if($stop_on_error) {
          // FIXME Log $e?
          break;
        }
      }
    }

    // Process the results
    foreach($this->processors as $processor) {
      $processor->process_results();
    }
  }

  public function test_class(string $test_class) {
    if(!isset($this->test_classes[$test_class]) || !class_exists($test_class)) {
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

    // Format the output
    if($this->processor) {
      $this->processor->add_test_results($id, $end['time']);
    }

    // Cleanup our test results.
    simpletest_clean_results_table($id);

    // Throw an exception if it failed
    if ($failed) {
      throw new DrushSimpleTestException("Test '$test_class' failed", $result);
    }

    return $result;
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

  public function __construct($message, string $detail=NULL) {
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
   * Method to add all test results from a test run.
   *
   * @param $id The ID in the database
   * @param $time The time (in milliseconds) it took to run the test
   */
  public function add_test_results(int $id, int $time) {
    // FIXME Store the time

    $results = db_query('SELECT * FROM {simpletest} WHERE test_id = %d ORDER BY test_class, message_id', $id);
    while($result = db_fetch_object($results)) {
      $this->add_result($result);
    }
  }

  /**
   * Function to process a single result. Is called by #add_test_results.
   *
   * @param $result A single result object as retrieved from the simpletest
   * database.
   */
  protected abstract function add_result($result);

  /**
   * Function to be called to process the results. For example, output to stdout
   */
  public abstract function process_results();
}

/**
 * Formats the output to JUnit compatible XML.
 */
class DrushSimpleTestXMLProcessor implements DrushSimpleTestProcessor {
  private $xml_dir;
  private $xml_files;

  public function __construct(string $xml_dir) {
    $this->xml_dir = $xml_dir;
    $this->xml_files = array();
  }

  public function add_result($result) {
    list($class, $name) = explode('->', $result->function, 2);

    if(!isset($this->xml_files[$class])) {
      $doc = new DomDocument('1.0');
      $root = $doc->createElement('testsuite');
      $root = $doc->appendChild($root);
      $this->xml_files[$class] = $doc;
    } else {
      $doc= $this->xml_files[$class];
    }

    // Save the result into the XML:
    $case = $doc->createElement('testcase');
    $case->setAttribute('classname', $class);
    $case->setAttribute('name', $name);

    if ($result->status == 'fail') {
      $fail = $doc->createElement('failure');
      $fail->setAttribute('type', 'failure');
      $fail->setAttribute('message', $result->message_group);
      $text = $doc->createTextNode($result->message);
      $fail->appendChild($text);
      $case->appendChild($fail);
    }
  }
}

/**
 * Formats the output and prints to stdout using drush_print.
 */
class DrushSimpleTestPrintProcessor implements DrushSimpleTestProcessor {
  private $results;
  private $verbose;
  private $color;

  public function __construct($verbose=FALSE, $color=TRUE) {
    $this->results = array();
    $this->verbose = $verbose;
    $this->color = $color;
  }

  public function add_result($result) {
    $this->results[] = $result;
  }

  /**
   * Format the result so that it fits within the default 80 character
   * terminal size.
   */
  public function process_results() {
    foreach($this->results as $result) {
      $summary = sprintf("%-10.10s %-30.30s %-5.5s\n",
          ucfirst($result->status), basename($result->file), $result->line);

      $this->print_message($summary, self::color_code($result->status));

      $lines = explode("\n", wordwrap(trim(strip_tags($result->message)), 76));
      foreach ($lines as $line) {
        drush_print('    '.$line);
      }
    }
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
  private function print_message(string $message, int $color_code) {
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
class DrushSimpleTestMailProcessor implements DrushSimpleTestProcessor {
  private $recipients;
  private $results;
  private $statuses;

  public function __construct(array $recipients) {
    $this->recipients = $recipients;
    $this->results = array();
    $this->statuses = array();
  }

  public function add_result($result) {
    $this->results[] = $result;

    if(!isset($this->statuses[$result->status])) {
      $this->statuses[$result->status] = 0;
    } else {
      $this->statuses[$result->status]++;
    }
  }

  /**
   * Processes the results by mailing
   */
  public function process_results() {
    if($this->results) {
      // Build subject
      $statuses = array();
      foreach($this->statuses as $status => $count) {
        $statuses[] = $count . " " . $status;
      }
      $subject = "Test results - " . implode(", ", $statuses);

      // FIXME implement me
      $output = "Test results\n\n";
      foreach($results as $result) {
        $output .= "Class: " . $result->class . "\n";
        $output .= wordwrap(trim(strip_tags($result->message)), 79);
      }

      // Mail the results
      mail(implode(",", $recipients), $subject, $output);
    }
  }
}
?>
