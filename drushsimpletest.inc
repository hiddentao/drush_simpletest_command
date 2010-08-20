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
    return array_key_exists($test_class, $this->test_classes) && class_exists($test_class) && method_exists($test_class, 'run');
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

  /**
   * Cleans the simpletest environment.
   */
  public function clean() {
    simpletest_clean_environment();
  }
}

/**
 * Exception class with a detail message
 */
class DrushSimpleTestException extends Exception {
  /** A more detailed description */
  private $detail;

  /**
   * Constructor
   */
  public function __construct($message, $detail=NULL) {
    parent::__construct($message);
    $this->detail = $detail;
  }

  /**
   * Returns a more detailed description.
   */
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
    $this->tests[$class] = array(
      'time' => $time,
      'functions' => array(),
      'summary' => array(),
    );
  }

  /**
   * Function to process a single result. Is called by #add_suite.
   *
   * @param $result A single result object as retrieved from the simpletest
   * database.
   */
  public function add_result($result, $time=0) {
    if(strpos('->', $result->function)) {
      list($class, $name) = explode('->', $result->function, 2);
    } else {
      $class = $result->file;
      $name = $result->function;
    }

    if(!isset($this->tests[$class])) {
      $this->add_suite($class);
    }

    $test = $this->tests[$class];

    if(!isset($test['functions'][$name])) {
      $test['functions'][$name] = array();
    }

    $f_res['time'] = $time;
    $f_res['status'] = $result->status;
    $f_res['message_group'] = $result->message_group;
    $f_res['message'] = htmlspecialchars_decode($result->message, ENT_QUOTES);
    $f_res['file'] = $result->file;
    $f_res['line'] = $result->line;

    $this->tests[$class]['functions'][$name][] = $f_res;

    // Update the status summary
    if(array_key_exists($result->status, $test['summary'])) {
      $test['summary'][$result->status]++;
    } else {
      $test['summary'][$result->status] = 1;
    }
  }

  /**
   * Function to be called to process the results. For example, output to stdout.
   */
  public abstract function process_results();
}

/**
 * Formats the output to JUnit compatible XML.
 */
class DrushSimpleTestXMLProcessor extends DrushSimpleTestProcessor {
  /** The XML destination directory */
  private $xml_dir;

  /**
   * Constructor
   */
  public function __construct($xml_dir) {
    $this->xml_dir = $xml_dir;
  }

  /**
   * Processes the results by transforming it into XML.
   */
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
      $root->setAttribute('name', $class);
      $root->setAttribute('time', $results['time']);
      // FIXME parse $results['summary'] into failed, exception and such

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
  /** @var bool Whether to use color */
  private $color;

  /**
   * Constructor
   *
   * @param bool $color Whether to use color
   */
  public function __construct($color=TRUE) {
    $this->color = $color;
  }

  /**
   * Format the result so that it fits within the default 80 character
   * terminal size.
   */
  public function process_results() {
    foreach($this->tests as $class => $results) {
      // Each function
      foreach($results["functions"] as $function => $messages) {
        // Each message
        foreach($messages as $result) {
        // print summary
          $summary = sprintf("%-10.10s %-30.30s %-5.5s\n",
              ucfirst($result["status"]), basename($result["file"]),
              $result["line"]);
          $this->print_message($summary, self::color_code($result["status"]));

          $lines = explode("\n", trim(strip_tags($result["message"])));
          $color_code = $this->color_code($result["status"]);
          foreach ($lines as $line) {
            $this->print_message("    ".$line, $color_code);
          }
        }
      }
      $this->print_message("");
    }
  }

  /**
   * Get the color code associated with the specified status.
   *
   * @param $status The status string to get code for.
   * @return int Color code.
   */
  private static function color_code($status) {
    switch ($status) {
      case 'Pass':
        return 32; // Green
      case 'Fail':
        return 31; // Red
      case 'Exception':
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
  private function print_message($message, $color_code=0) {
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
  /** @var array The recipients of the test results */
  private $recipients;

  /**
   * Constructor
   *
   * @param array $recipients The recipients of the tests results
   */
  public function __construct(array $recipients) {
    $this->recipients = $recipients;
  }

  /**
   * Processes the results by mailing
   */
  public function process_results() {
    $statuses = array();
    $message = "Test results\n\n";

    // Create the message
    foreach($this->tests as $class => $results) {
      $message .= "= Class: " . $class . "\n";

      // Update the status summary
      foreach($results['summary'] as $status => $count) {
        if(array_key_exists($status, $statuses)) {
          $statuses[$status] += $count;
        } else {
          $statuses[$status] = $count;
        }
      }

      // Add each message
      foreach($results['functions'] as $function => $messages) {
        $message .= "== Function: " . $function . "\n";
        foreach($messages as $result) {
          $message .= wordwrap(trim(strip_tags($result['message'])), 70)."\n";
        }
        $message .= "\n\n";
      }
    }

    if($statuses) {
      // Build subject
      $statuses_strings = array();
      foreach($statuses as $status => $count) {
        $statuses_strings[] = $count . " " . $status;
      }
      $subject = "Test results - " . implode(", ", $statuses_strings);

      // Mail the results
      mail(implode(",", $this->recipients), $subject, $message);
    }
  }
}
