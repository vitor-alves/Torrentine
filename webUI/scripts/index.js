const { Header, Content, Footer, Sider } = antd.Layout;


const columns = [
  { title: 'Full Name', width: 100, dataIndex: 'name', key: 'name', fixed: 'left' },
  { title: 'Age', width: 100, dataIndex: 'age', key: 'age'},
  { title: 'Column 1', dataIndex: 'address', key: '1', width: 150 },
  { title: 'Column 2', dataIndex: 'address', key: '2', width: 150 },
  { title: 'Column 3', dataIndex: 'address', key: '3', width: 150 },
  { title: 'Column 4', dataIndex: 'address', key: '4', width: 150 },
  { title: 'Column 5', dataIndex: 'address', key: '5', width: 150 },
  { title: 'Column 6', dataIndex: 'address', key: '6', width: 150 },
  { title: 'Column 7', dataIndex: 'address', key: '7', width: 150 },
  { title: 'Column 8', dataIndex: 'address', key: '8' },
  {
    title: 'Action',
    key: 'operation',
    width: 100,
    render: () => <a href="#">action</a>,
  },
];

const data = [];
for (let i = 0; i < 50; i++) {
  data.push({
    key: i,
    name: `Edrward ${i}`,
    age: 32,
    address: `London Park no. ${i}`,
  });
}

var destination = document.querySelector("#container");




var SiderDemo = React.createClass({

	state: {
   	 collapsed: false
  	},
  	toggle: function() {
    	this.setState({
     	 collapsed: !this.state.collapsed,
    })},

	render: function() {
		return(

			<antd.Layout>
        <Sider
          trigger={null}


        >
          <div className="logo" />
          <antd.Menu theme="dark" mode="inline" defaultSelectedKeys={['1']}>
            <antd.Menu.Item key="1">
              <antd.Icon type="user" />
              <span>nav 1</span>
            </antd.Menu.Item>
            <antd.Menu.Item key="2">
              <antd.Icon type="video-camera" />
              <span>nav 2</span>
            </antd.Menu.Item>
            <antd.Menu.Item key="3">
              <antd.Icon type="upload" />
              <span>nav 3</span>
            </antd.Menu.Item>
          </antd.Menu>
        </Sider>
        <antd.Layout>
          <Header style={{ background: '#fff', padding: 0 }}>
            <antd.Icon
              className="trigger"


            />
          </Header>
          <Content style={{ margin: '24px 16px', padding: 24, background: '#fff', minHeight: 280 }}>
            Content
          </Content>
        </antd.Layout>
      </antd.Layout>

			);
	}
});


ReactDOM.render(<div>
				
				<SiderDemo />
				
				</div>, destination);