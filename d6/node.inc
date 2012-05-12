<?php

/**
 * Handling specific to a Drupal 6 source for nodes.
 */
class DrupalNode6Migration extends DrupalNodeMigration {
  /**
   * @param array $arguments
   */
  public function __construct(array $arguments) {
    // Basic CCK field suffixes
    $this->fieldSuffixes = array(
      'computed' => 'value',
      'date' => 'value',
      'datestamp' => 'value',
      'datetime' => 'value',
      'number_decimal' => 'value',
      'number_integer' => 'value',
      'number_float' => 'value',
      'text' => 'value',
      'filefield' => 'fid', // @todo: Handle list and data suffixes
      'file' => 'fid',
      'nodereference' => 'nid',
    );

    parent::__construct($arguments);
  }


  /**
   * Set up field mappings for cck fields
   */
  protected function doFieldMappings(&$query) {
    $destinationFields = $this->destination->fields();

    // Load up field data for dynamically mapped fields
    foreach ($this->sourceFieldTypes as $field_name => $field_type) {
      // Find the data. Simple unshared unmultiple fields will be in
      // content_type_{$this->sourceType}. Others will be in
      // content_$field_name.
      // TODO: Handle multi-value fields
      $table = "content_$field_name";
      if (!Database::getConnection('default', $this->sourceConnection)
        ->schema()->tableExists($table)) {
        $table = 'content_type_' . $this->sourceType;
      }
      if (isset($this->fieldSuffixes[$field_type])) {
        $suffix = $this->fieldSuffixes[$field_type];
        $field_field = $field_name .'_'. $suffix;
        $field_field_format = $field_name .'_format';
        $field_format = $field_name .':format';
        $field_language = $field_name .':language';

        $query->leftJoin($table, $table, $table.'.vid=n.vid');
        $query->fields($table, array($field_field));
        switch($field_type) {
          case 'file':
          case 'filefield':
              $this->addFieldMapping($field_name, $field_field);
              $this->addFieldMapping($field_name.':file_class')
                ->defaultValue('MigrateFileFid');
            break;
          case 'text_long':
          case 'text':
            // Value
            $query->fields($table, array($field_field));
            $this->addFieldMapping($field_name, $field_field);
            // Format
            if (isset($destinationFields[$field_format])) {
              if (Database::getConnection('default', $this->sourceConnection)
                  ->schema()
                  ->fieldExists($table, $field_field_format)) {
                $query->fields($table, array($field_field_format));
                $this->addFieldMapping($field_format, $field_field_format);
              } else {
                $this->addUnmigratedDestinations(array($field_format));
              }
            }
            // Language @TODO
            if (isset($destinationFields[$field_language])) {
              $this->addUnmigratedDestinations(array($field_language));
            }
            break;
          case 'number_integer':
          case 'number_float':
          case 'date':
            $query->fields($table, array($field_field));
            $this->addFieldMapping($field_name, $field_field);
            break;
          case 'link':

            break;
          default:

        }
        // Some fields will need translation (e.g., from a text string to a boolean)
        $method = "handle_$field_name";
        if (method_exists($this, $method)) {
          $row->$field_name = $this->$method($row->$field_name);
        }
      }
    }

  }


  /**
   * Process uploads
   * @param $query
   */
  protected function doUploads(&$query) {

  }

  /**
   * Query for basic node fields from Drupal 6.
   *
   * @return QueryConditionInterface
   */
  protected function nodeQuery() {
    $query = Database::getConnection('default', $this->sourceConnection)
             ->select('node', 'n')
             ->fields('n', array('nid', 'vid', 'language', 'title', 'uid',
               'status', 'created', 'changed', 'comment', 'promote', 'moderate',
               'sticky'))
             ->condition('type', $this->sourceType);
    $query->innerJoin('node_revisions', 'nr', 'n.vid=nr.vid');
    $query->fields('nr', array('body', 'teaser', 'format'));
    $this->doFieldMappings($query);
    $this->doUploads($query);
    return $query;
  }

  /**
   * Retrieves the available fields for this content type from Drupal 6.
   *
   * @return array
   */
  public function sourceFieldList() {
    migrate_instrument_start('DrupalNode6Migration::sourceFieldList');
    $fields = array();
    $this->sourceFieldTypes = array();

    // Get each CCK field attached to this type.
    if (db_table_exists('content_node_field_instance')) {
      $query = Database::getConnection('default', $this->sourceConnection)
               ->select('content_node_field_instance', 'i')
               ->fields('i', array('label'))
               ->condition('i.type_name', $this->sourceType);
      $query->innerJoin('content_node_field', 'f', 'i.field_name = f.field_name');
      $query->fields('f', array('field_name', 'type'));
      $result = $query->execute();
      foreach ($result as $row) {
        $fields[trim($row->field_name)] = t('!label (!type)',
          array('!label' => $row->label, '!type' => $row->type));
        $this->sourceFieldTypes[$row->field_name] = $row->type;
      }
    }
    // Get each vocabulary attached to this type.
    $query = Database::getConnection('default', $this->sourceConnection)
             ->select('vocabulary_node_types', 'vnt')
             ->fields('vnt', array('vid'));
    $query->innerJoin('vocabulary', 'v', 'vnt.vid=v.vid');
    $query->addField('v', 'name');
    $query->condition('vnt.type', $this->sourceType);
    $result = $query->execute();
    foreach ($result as $row) {
      $fields[$row->vid] = t('!label (Taxonomy Term)',
                             array('!label' => $row->name));
      $this->sourceFieldTypes[$row->vid] = 'taxonomy_term';
    }
    migrate_instrument_stop('DrupalNode6Migration::sourceFieldList');
    return $fields;
  }

  /**
   * Called after the basic query data is fetched - we'll use this to populate
   * the source row with the CCK fields.
   */
  public function prepareRow($row) {
    if (parent::prepareRow($row) === FALSE) {
      return FALSE;
    }


    // And. load up the data for taxonomy terms
    $query = Database::getConnection('default', $this->sourceConnection)
             ->select('term_node', 'tn')
             ->fields('tn', array('tid'))
             ->condition('tn.vid', $row->vid);
    $query->innerJoin('term_data', 'td', 'tn.tid=td.tid');
    $query->fields('td', array('vid'));
    $result = $query->execute();
    foreach ($result as $term_row) {
      $row->{$term_row->vid}[] = $term_row->tid;
    }
  }

  /**
   * Translate the fid into the file path MigrateFileFieldHandler expects.
   * The path to the D6 instance must be passed in separately as source_path.
   * @see MigrateFileFieldHandler::arguments()
   */
  public function handle_type_filefield($fid) {
    $filepath = Database::getConnection('default', $this->sourceConnection)
      ->select('files', 'f')
      ->fields('f', array('filepath'))
      ->condition('fid', $fid)
      ->execute()
      ->fetchField();
    return $filepath;
  }
}
