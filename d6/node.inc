<?php

/**
 * Handling specific to a Drupal 6 source for nodes.
 */
class DrupalNode6Migration extends DrupalNodeMigration {

  protected $vids = array();
  protected $destinationFields = array();
  protected $multipleFields = array();

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
      'userreference' => 'uid',
    );
    parent::__construct($arguments);
    $this->addFieldMapping('body:summary', 'teaser');
    $this->addFieldMapping('body:format', 'format');
    $this->addFieldMapping('body:language', 'language');

    /* Highwater mark
    $this->highwaterField = array(
      'name' => 'changed',
      'alias' => 'n',
      'type' => 'int',
    );//*/
  }


  /**
   * Set up taxonomy mappings for the node
   */
   protected function setVocabs() {
     // Get each vocabulary attached to this type.
     $vocabQuery = Database::getConnection('default', $this->sourceConnection)
       ->select('vocabulary_node_types', 'vnt')
       ->fields('vnt', array('vid'));
     $vocabQuery->innerJoin('vocabulary', 'v', 'vnt.vid=v.vid');
     $vocabQuery->addField('v', 'name');
     $vocabQuery->condition('vnt.type', $this->sourceType);
     $result = $vocabQuery->execute();

     foreach ($result as $vocab) {
       $this->vids[] = $vocab->vid;
     }
   }


    protected function doTermMappings() {
      foreach ($this->vids as $vid) {
        $this->addFieldMapping('taxonomy_vocabulary_'. $vid)->defaultValue(array());
        $this->addFieldMapping('taxonomy_vocabulary_'. $vid .':source_type')->defaultValue('tid');
        $this->addFieldMapping('taxonomy_vocabulary_'. $vid .':create_term')->defaultValue(FALSE);
      }
    }


  /**
   * Set up field mappings for cck fields
   * @param $query
   */
  protected function doFieldMappings(&$query) {
    $this->destinationFields = $this->destination->fields();

    // Prepare query for dealing with multiple rows if required
    $query->groupBy('n.nid');

    // Load up field data for dynamically mapped fields
    foreach ($this->sourceFieldTypes as $field_name => $field_type) {
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

        // Set up field names and select the field
        $field_field = $field_name .'_'. $this->fieldSuffixes[$field_type];
        $query->fields($table, array($field_field));

        // Deal with multivalue fields

        if ($this->getCardinality($field_name, $field_field) != 1) {
          $query->addExpression('GROUP_CONCAT(DISTINCT '. $table .'.'. $field_field .' SEPARATOR \'|\')', $field_field .'_list');
        }

        // Add field mappings, and select any data fields
        switch($field_type) {
          case 'file':
          case 'filefield':
              $this->addFileFieldMapping($query, $table, $field_name, $field_field);
            break;
          case 'text':
            $this->addTextFieldMapping($query, $table, $field_name, $field_field);
            break;
          case 'number_decimal':
          case 'number_integer':
          case 'number_float':
          case 'nodereference':
          case 'userreference':
            $this->addFieldMapping($field_name, $field_field);
            break;
          case 'date':
          case 'datestamp':
            $arguments = array('timezone' => 'Europe/London');
            $this->addFieldMapping($field_name, $field_field)->arguments($arguments);
            break;
          case 'link':
            break;
          default:
        }
      }
    }
  }


  /**
   * Get the cardinality of a field
   * @param $field_name
   * @param $field_field
   * @return mixed
   */
  protected function getCardinality($field_name, $field_field) {
    $field_info = field_info_field_by_ids();
    $field_instance = field_info_instance('node', $field_name,  $this->sourceType);
    $cardinality = $field_info[$field_instance['field_id']]['cardinality'];
    if ($cardinality !=1) {
      $this->multipleFields[$field_name] = $field_field;
    }
    return $cardinality;
  }


  /**
   * @param $query
   * @param $table
   * @param $field_name
   * @param $field_field
   */
  protected function addFileFieldMapping(&$query, $table, $field_name, $field_field) {
    $query->fields($table, array($field_name .'_data'));
    $this->addFieldMapping($field_name, $field_field);
    $this->addFieldMapping($field_name.':file_class')
      ->defaultValue('MigrateFileFid');

    // Deal with data from multi value fields, , we will set them in $this->prepare()
    if ($this->getCardinality($field_name, $field_field) != 1) {
      $query->addExpression('GROUP_CONCAT(DISTINCT '. $table .'.'. $field_name . '_data SEPARATOR \'|\')', $field_name .'_data_list');
    }

    // Deal with alt and title, we will set them in $this->prepareRow()
    if ($this->destinationFields[$field_name .':alt']) {
      $this->addFieldMapping($field_name .':alt', $field_name . '_alt');
      $this->addFieldMapping($field_name .':title', $field_name . '_title');
    }
    // Deal with description and display if they exist, we will set them in $this->prepareRow()
    if ($this->destinationFields[$field_name .':description']) {
      $this->addFieldMapping($field_name .':description')->defaultValue('');
      $this->addFieldMapping($field_name .':display')->defaultValue(1);
    }
    // TODO Can files have a language in D6?
    $this->addUnmigratedDestinations(array($field_name .':language'));
  }


  /**
   * @param $query
   * @param $table
   * @param $field_name
   * @param $field_field
   */
  protected function addTextFieldMapping(&$query, $table, $field_name, $field_field) {
    // Value
    $this->addFieldMapping($field_name, $field_field);
    // Format
    if (isset($this->destinationFields[$field_name .':format'])) {
      if (Database::getConnection('default', $this->sourceConnection)
        ->schema()
        ->fieldExists($table, $field_name .'_format')) {
        $query->fields($table, array($field_name .'_format'));
        $this->addFieldMapping($field_name .':format', $field_name .'_format');
      } else {
        $this->addUnmigratedDestinations(array($field_name .':format'));
      }
    }
    // Language @TODO
    if (isset($this->destinationFields[$field_name .':language'])) {
      $this->addUnmigratedDestinations(array($field_name .':language'));
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
    $query->addExpression('GROUP_CONCAT(DISTINCT u.fid SEPARATOR \'|\')', 'fid_list');
    $this->addFieldMapping('upload', 'fid_list');
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
    $query->orderBy('changed');
    $this->doFieldMappings($query);
    $this->doUploads($query);
    $this->setVocabs();
    $this->doTermMappings();
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
    $this->sourceFieldWidgets = array();
    $this->sourceFieldTypes = array();

    // Get each CCK field attached to this type.
    if (db_table_exists('content_node_field_instance')) {
      $query = Database::getConnection('default', $this->sourceConnection)
               ->select('content_node_field_instance', 'i')
               ->fields('i', array('label', 'widget_type'))
               ->condition('i.type_name', $this->sourceType);
      $query->innerJoin('content_node_field', 'f', 'i.field_name = f.field_name');
      $query->fields('f', array('field_name', 'type'));
      $result = $query->execute();
      foreach ($result as $row) {
        $fields[trim($row->field_name)] = t('!label (!type)',
          array('!label' => $row->label, '!type' => $row->type));
        $this->sourceFieldWidgets[$row->field_name] = $row->widget_type;
        $this->sourceFieldTypes[$row->field_name] = $row->type;
      }
    }
    migrate_instrument_stop('DrupalNode6Migration::sourceFieldList');
    return $fields;
  }


  /**
   * Called after the basic query data is fetched - we'll use this to populate
   * the source row with the CCK fields.
   * @param $row
   * @return mixed
   */
  public function prepareRow($row) {
    if (parent::prepareRow($row) === FALSE) {
      return FALSE;
    }

    foreach($this->multipleFields as $field_name => $field_field) {
      if ($row->{$field_field .'_list'}) {
        $row->{$field_field} = explode('|', $row->{$field_field .'_list'});
      }
    }

    foreach($this->sourceFieldWidgets as $field_name => $widget_type) {
      // Some widget types will need extra support
      $method = "handle_widget_$widget_type";
      if (method_exists($this, $method)) {
        $this->$method($row, $field_name);
      }
    }

    foreach($this->destinationFields as $field_name => $field_label) {
      if  (isset($row->{$field_name .'_format'})) {
        $method = 'handle_format_'. $row->{$field_name .'_format'};
        if (method_exists($this, $method)) {
          $this->$method($row->{$field_name .'_value'});
        }
      }
    }

    foreach($this->sourceFieldTypes as $field_name => $type) {
      // Some fields will need translation (e.g., from a text string to a boolean)
      $method = "handle_$field_name";
      if (method_exists($this, $method)) {
        // Pass by reference
        $this->$method($row->{$field_name .'_value'});
      }
      // Set alt and title for filefields @TODO could move this into a handler for completeness
      if ($type == 'filefield' && $row->{$field_name .'_data'}) {
        $data = unserialize($row->{$field_name .'_data'});
        foreach ($data as $datatype => $datavalue) {
          $row->{$field_name .'_'. $datatype} = $datavalue;
        }
      }
    }
  }


  public function prepare(&$node, $row) {
    // And. load up the data for taxonomy terms @todo load this as a handler
    $query = Database::getConnection('default', $this->sourceConnection)
      ->select('term_node', 'tn')
      ->fields('tn', array('tid'))
      ->condition('tn.vid', $row->vid);
    $query->innerJoin('term_data', 'td', 'tn.tid=td.tid');
    $query->fields('td', array('vid'));
    $result = $query->execute();
    foreach ($result as $term) {
      $node->{'taxonomy_vocabulary_'. $term->vid}['und'][] = array('tid' => $term->tid);
    }

    // Set is_new on the $node object, we will always have a nid in our case
    $result = db_query("SELECT nid, vid FROM {node} WHERE nid = " . $row->nid)->fetch();
    $node->is_new = !($result->nid);

    // Remove any previously entered fields and data
    if ($node->is_new) {
      $info = field_info_fields();
      foreach ($info as $field) {
        $storage = $field['storage']['details']['sql']['FIELD_LOAD_CURRENT'];
        foreach ($storage as $table => $key) {
          db_query("DELETE FROM $table WHERE entity_id = {$row->nid}");
        }
      }
      db_query("DELETE FROM node_access WHERE nid = {$row->nid}");
    }

    // Remove menu object from node as this seems to break menus.
    // Deletes hidden parent menu items if they are disabled.
    if (isset($node->menu)) unset($node->menu);

    if (module_exists('path') && isset($node->path)) {
      $node->path['pid'] = db_select('url_alias')
        ->condition('source', 'node/'. $node->nid, '=')
        ->fields('url_alias', array('pid'))
        ->execute()
        ->fetchField(0);
      // Need to set the node language so that the path gets the correct language. @see path_node_update().
      $node->language = LANGUAGE_NONE;
    }

    // Redo file attachment support so we can add multiple items.
    $query = Database::getConnection('default', $this->sourceConnection)
      ->select('upload', 'u')
      ->fields('u');
    $query->condition('vid', $row->vid);
    $files = $query->execute();
    $node->upload['und'] = array();
    foreach ($files as $file) {
      $node->upload['und'][] = array(
        'fid' => $file->fid,
        'description' => $file->description,
        'display' => 1,
      );
    }

    // Data for multivalue file fields
    foreach($this->multipleFields as $field_name => $field_field) {
      if ($row->{$field_name .'_data_list'}) {
        $datum = explode('|', $row->{$field_name .'_data_list'});
        $i = 0;
        foreach ($datum as $data_serialized) {
          $data = unserialize($data_serialized);
          $node->{$field_name}['und'][$i] = array_merge($node->{$field_name}['und'][$i], $data);
          $i++;
        }
      }
    }


    // Add xml sitemap support
    $query = Database::getConnection('default', $this->sourceConnection)
      ->select('xmlsitemap', 'x')
      ->fields('x');
    $query->condition('id', $row->nid);
    $query->condition('type', 'node');
    $result = $query->execute();
    $xmlsitemap = $result->fetchObject();
    $xmlsitemap->language = $xmlsitemap->language ? $xmlsitemap->language : LANGUAGE_NONE;
    drupal_write_record('xmlsitemap', $xmlsitemap, array('id', 'type') );
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


  /**
   * Translate the fid into the file path MigrateFileFieldHandler expects.
   * The path to the D6 instance must be passed in separately as source_path.
   * @see MigrateFileFieldHandler::arguments()
   */
  public function handle_widget_imagefield_crop_widget($row, $field_name) {
    $imagefield_crop_info = variable_get('imagefield_crop_info', array());
    $data = unserialize($row->{$field_name .'_data'});
    $imagefield_crop_info[$row->{$field_name .'_fid'}] = $data['crop'];
    variable_set('imagefield_crop_info', $imagefield_crop_info);
  }


}
