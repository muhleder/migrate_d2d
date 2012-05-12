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
   * Set up taxonomy mappings for the node
   * @param $query
   */
   protected function doTermMappings(&$query) {
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
   }

  /**
   * Set up field mappings for cck fields
   * @param $query
   */
  protected function doFieldMappings(&$query) {
    $destinationFields = $this->destination->fields();
    $node_info = entity_get_property_info('node');
    $content_type_properties = $node_info['bundles'][$this->sourceType]['properties'];

    // Load up field data for dynamically mapped fields
    foreach ($this->sourceFieldTypes as $field_name => $field_type) {
      $destinationFieldType = $content_type_properties[$field_type]['type'];
      // TODO: Handle multi-value fields

      if (isset($this->fieldSuffixes[$field_type])) {
        // Join to table
        $table = "content_$field_name";
        if (!Database::getConnection('default', $this->sourceConnection)
          ->schema()->tableExists($table)) {
          $tables = $query->getTables();
          $table = 'content_type_' . $this->sourceType;
          if (!$tables[$table]) {
            $query->join($table, $table, $table.'.vid=n.vid');
          }
        } else {
          $query->leftJoin($table, $table, $table.'.vid=n.vid');
        }

        // Set up field names
        $suffix = $this->fieldSuffixes[$field_type];
        $field_field = $field_name .'_'. $suffix;
        $field_field_format = $field_name .'_format';
        $field_format = $field_name .':format';
        $field_language = $field_name .':language';

        // Select value field
        $query->fields($table, array($field_field));
        // Add field mappings, and select any data fields
        switch($field_type) {
          case 'file':
          case 'filefield':
              $query->fields($table, array($field_name .'_data'));
              $this->addFieldMapping($field_name, $field_field);
              $this->addFieldMapping($field_name.':file_class')
                ->defaultValue('MigrateFileFid');
              // Deal with alt and title, we will set them in $this->prepareRow()
              if ($destinationFields[$field_name .':alt']) {
                $this->addFieldMapping($field_name .':alt')->defaultValue('');
                $this->addFieldMapping($field_name .':title')->defaultValue('');
              }
              // Deal with description and display if they exist
              if ($destinationFields[$field_name .':description']) {
                $query->fields($table, array(
                  $field_name .'_description',
                  $field_name .'_list',
                ));
                $this->addFieldMapping($field_name .':description', $field_name .'_description');
                $this->addFieldMapping($field_name .':display', $field_name .'_list');
              }
              // TODO Can files have a language in D6?
              $this->addUnmigratedDestinations(array($field_language));
            break;
          case 'text_long':
          case 'text':
            // Value
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
            $this->addFieldMapping($field_name, $field_field);
            break;
          case 'link':

            break;
          default:
        }
      }
    }
  }


  /**
   * Process uploads
   * @param $query
   */
  protected function doUploads(&$query) {
    $query->leftJoin('upload', 'u', 'u.vid=n.vid');
    $query->fields('u', array(
      'fid',
      'description',
      'list',
      'weight',
    ));
    $this->addFieldMapping('upload', 'fid');
    $this->addFieldMapping('upload:file_class')
      ->defaultValue('MigrateFileFid');
    $this->addFieldMapping('upload:description', 'description');
    $this->addFieldMapping('upload:display', 'list');
    $this->addUnmigratedDestinations(array('upload:language',));
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

    foreach($this->sourceFieldTypes as $field_name => $type) {
      // Some fields will need translation (e.g., from a text string to a boolean)
      $method = "handle_$field_name";
      if (method_exists($this, $method)) {
        // Pass by reference
        $this->$method($row->{$field_name .'_value'});
      }
      // Set alt and title for filefields
      if ($type == 'filefield' && $row->{$field_name .'_data'}) {
        $data = unserialize($row->{$field_name .'_data'});
        $row->{$field_name .':alt'} = $data['alt'];
        $row->{$field_name .'title'} = $data['title'];
      }
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
